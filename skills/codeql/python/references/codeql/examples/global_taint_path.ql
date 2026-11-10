/**
 * @name Generic global taint path
 * @description Demonstrates the pinned modular global taint API and path-query signature.
 * @kind path-problem
 * @problem.severity warning
 * @precision high
 * @id checker-gen/example/global-taint-path
 * @tags security
 */

import python
import semmle.python.ApiGraphs
import semmle.python.dataflow.new.DataFlow
import semmle.python.dataflow.new.TaintTracking

module ExampleConfig implements DataFlow::ConfigSig {
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

module ExampleFlow = TaintTracking::Global<ExampleConfig>;
import ExampleFlow::PathGraph

from ExampleFlow::PathNode source, ExampleFlow::PathNode sink
where ExampleFlow::flowPath(source, sink)
select sink.getNode(), source, sink, "Unsafe flow from $@.", source.getNode(), "source"
