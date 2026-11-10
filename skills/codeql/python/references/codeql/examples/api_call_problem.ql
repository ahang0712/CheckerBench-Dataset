/**
 * @name Generic imported API call
 * @description Demonstrates stable imported-API identity and a local report site.
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id checker-gen/example/imported-api-call
 * @tags security
 */

import python
import semmle.python.ApiGraphs
import semmle.python.dataflow.new.DataFlow

from API::CallNode call
where
  call = API::moduleImport("example_package")
              .getMember("dangerous_operation")
              .getACall()
select call.asExpr(), "This call reaches the example dangerous operation."
