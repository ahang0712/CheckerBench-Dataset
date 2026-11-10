# Value-flow and taint-query patterns

Use value flow when the vulnerability depends on provenance. Use taint tracking when value-preserving
transformations should propagate untrustedness.

## Local flow first

For flow inside one callable, try:

```ql
DataFlow::localFlow(sourceNode, sinkNode)
TaintTracking::localTaint(sourceNode, sinkNode)
```

Both operands are `DataFlow::Node`. Common conversions are `node.asExpr()`,
`node.asCfgNode()`, and `DataFlow::exprNode(expr)`. `API::CallNode` is also a
`DataFlow::CallCfgNode`; its arguments are `getArg(i)`, `getArgByName(name)`,
`getStarArg()`, and `getKwargs()`.

## Global taint skeleton

Use the current modular API:

```ql
import python
import semmle.python.ApiGraphs
import semmle.python.dataflow.new.DataFlow
import semmle.python.dataflow.new.TaintTracking

module Config implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node source) {
    source = API::moduleImport("example_source").getMember("read").getACall()
  }

  predicate isSink(DataFlow::Node sink) {
    sink = API::moduleImport("example_sink").getMember("consume").getACall().getArg(0)
  }

  predicate isBarrier(DataFlow::Node node) {
    node = API::moduleImport("example_safety").getMember("sanitize").getACall()
  }

  predicate observeDiffInformedIncrementalMode() { any() }
}

module Flow = TaintTracking::Global<Config>;
import Flow::PathGraph

from Flow::PathNode source, Flow::PathNode sink
where Flow::flowPath(source, sink)
select sink.getNode(), source, sink, "Unsafe flow from $@.", source.getNode(), "source"
```

Use `DataFlow::Global<Config>` instead when only value-preserving steps should flow. Add
`isAdditionalFlowStep` only for a justified semantic transformation; broad edges can make every
value reach every sink.

## Guard and barrier discipline

A sanitizer call can be a direct `isBarrier`. A branch-dependent validation usually requires
`DataFlow::GuardNode` and `DataFlow::BarrierGuard<...>`; do not model it as “a comparison exists
somewhere”. Verify that the guard is about the same flowed value and controls the safe path reaching
the sink.

Before finalizing:

1. Every before finding is the intended sink in a patch-overlapping semantic unit.
2. The after finding disappears because the safety condition is modeled.
3. Equivalent code with renamed locals would still match.
4. A path query's SARIF path contains meaningful source-to-sink evidence.
