/**
 * @name Generic local API flow
 * @description Demonstrates local flow between a source call and a sink argument.
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id checker-gen/example/local-api-flow
 * @tags security
 */

import python
import semmle.python.ApiGraphs
import semmle.python.dataflow.new.DataFlow

from API::CallNode sourceCall, API::CallNode sinkCall
where
  sourceCall = API::moduleImport("example_source").getMember("read").getACall() and
  sinkCall = API::moduleImport("example_sink").getMember("consume").getACall() and
  DataFlow::localFlow(sourceCall, sinkCall.getArg(0))
select sinkCall.asExpr(), "A locally sourced value reaches this sink."
