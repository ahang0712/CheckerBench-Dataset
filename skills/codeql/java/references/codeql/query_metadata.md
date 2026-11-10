# Query-pack and result contract

The main detector is `query/vulnerability.ql`. Keep `query/qlpack.yml` offline-resolvable against
the bundled `codeql/python-all` version. A `problem` query selects `(location, message)`; a
`path-problem` query follows the CodeQL path-query select signature and must report the real sink.

For a modular flow named `Flow`, the pinned path-query shape is:

```ql
import Flow::PathGraph

from Flow::PathNode source, Flow::PathNode sink
where Flow::flowPath(source, sink)
select sink.getNode(), source, sink, "Unsafe flow from $@.", source.getNode(), "source"
```

The first selected column remains the vulnerable sink location. The next two columns are the path
endpoints required by `@kind path-problem`.

Required metadata includes `@name`, `@description`, `@kind`, `@id`, `@problem.severity`,
`@precision`, and security tags. Keep exactly one `@id`, preserving the task-unique
`expected_query_id` provisioned in `config.json` and in the placeholder query. The ID is metadata
only and must not participate in query predicates. The final report location must identify the
vulnerable operation, not merely a surrounding function, patch guard, or source.

Validation order is fixed: compile, run before, inspect SARIF, run after, inspect SARIF, then check
the final query metadata/report site and write `summary.json`. Hard success requires a patch-relevant
before finding that disappears after the fix; warning-count reduction alone is insufficient.
