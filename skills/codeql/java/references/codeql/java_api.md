# Java CodeQL API navigation (codeql/java-all 9.2.4)

The environment uses CodeQL 2.26.4 with `codeql/java-all` 9.2.4. Search the
installed official sources with `scripts/search_codeql_api.sh PATTERN`; do not
browse the network or guess a predicate name.

## Import and core call / method types

The top-level `import java` exposes `Call`, `Method`, `MethodAccess`, `Class`,
`RefType`, `CompileTimeConstantExpr`, and the standard `Location` predicates.
For call/method accesses, `MethodAccess` is exported at top level in java-all;
prefer `Call` (the value-flow call node) only once you need data-flow. Use
`import java` plus, when needed, the dedicated library modules:

```ql
import java
import semmle.code.java.dataflow.DataFlow
import semmle.code.java.dataflow.TaintTracking
```

## Matching a method invocation by declared name

```ql
import java

from MethodAccess ma, Method m
where
  m = ma.getMethod() and
  m.hasName("getInstance") and
  ma.getArgument(0).(CompileTimeConstantExpr).getStringValue() = "MD5"
select ma, "MessageDigest.getInstance with MD5"
```

`Method.hasName(name)` matches the method's simple name. For a qualified
declaring class use `m.getDeclaringType().hasQualifiedName("javax.crypto", "Cipher")`.

## Value flow from a source to a sink

```ql
import java
import semmle.code.java.dataflow.DataFlow

from DataFlow::Node source, DataFlow::Node sink, MethodAccess call
where
  source.asExpr() = call.getArgument(0) and
  sink.asExpr() = call and
  DataFlow::localFlow(source, sink)
select call, "Argument flows to the call"
```

## Configuration classes / taint paths

Taint configurations subclass `TaintTracking::Configuration`, overriding
`isSource` and `isSink` (both take `DataFlow::Node`). See the installed
`codeql/java-all` under `semmle/code/java/dataflow/TaintTracking.qll` for the
exact signature and use `scripts/search_codeql_api.sh 'class Configuration'`.

## Compile-time string constants

A string literal argument is exposed as a `CompileTimeConstantExpr`; call
`getStringValue()` for its value. Do not compare `Expr.toString()` or a raw
column/source text.

## Avoid patch-local matching

Do not use a patch-local file path, line number, commit hash, label, or local
variable name as a primary matching condition. Report at the vulnerable call /
sink / field access. A project/framework public API is allowed as the intended
vulnerability family only when `scope_statement.md` justifies it.

## Search recipes

```bash
scripts/search_codeql_api.sh 'class MethodAccess'
scripts/search_codeql_api.sh 'string hasQualifiedName'
scripts/search_codeql_api.sh 'predicate localFlow'
scripts/search_codeql_api.sh 'class TaintTracking'
scripts/search_codeql_api.sh 'string getStringValue'
```

Read one declaration and one official use site, then implement the main query.
Do not create a new inspection query merely to rediscover an API already
present in the installed sources.
