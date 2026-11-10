# AST and control-flow design patterns

Translate the patch into four facts before coding: vulnerable operation, trigger condition, safety
condition, and CodeQL-observable evidence.

## Raw call syntax

Use this only when syntactic call shape is the intended mechanism or when API graphs cannot identify
a project-defined operation:

```ql
import python

from Call call, Attribute target
where
  call.getFunc() = target and
  target.getName() = "stable_public_operation" and
  call.getPositionalArgumentCount() = 1
select call, "Potentially unsafe operation."
```

Useful pinned AST accessors:

- `Call.getFunc()`, `getPositionalArg(i)`, `getAPositionalArg()`,
  `getPositionalArgumentCount()`;
- `Call.getANamedArg()`; a `Keyword` has `getArg()` for its name and `getValue()`;
- `Call.getStarargs()` and `getKwargs()` for `*args` and `**kwargs`;
- `Attribute.getObject()` and `getName()`.

For stable imported APIs, use `API::CallNode` from `api_navigation.md` instead of matching only
an attribute name.

## AST to CFG mapping

`ControlFlowNode.getNode()` is the non-deprecated mapping to its AST node:

```ql
exists(ControlFlowNode guardCfg, ControlFlowNode sinkCfg |
  guardCfg.getNode() = guardExpr and
  sinkCfg.getNode() = sinkCall and
  guardCfg.dominates(sinkCfg)
)
```

`dominates` includes the node itself; use `strictlyDominates` when equality must be excluded.
`strictlyReaches` proves possible order, not that every path passes the guard.

Dominance alone does **not** prove the safe branch. For `if unsafe: return`, the sink must be
controlled by the false successor; for `if safe: sink`, it must be controlled by the true
successor. The pinned data-flow library exposes:

```ql
exists(DataFlow::GuardNode guard, boolean safeBranch |
  guard.controlsBlock(sinkNode.asCfgNode().getBasicBlock(), safeBranch)
)
```

The query must additionally relate `guard` to the same value as the sink and prove which branch is
safe. Search the official bundle for `BarrierGuard<` and `controlsBlock` before implementing a
complex guard.

## Soundness checks

- A comparison elsewhere in the function is not a guard.
- `strictlyReaches` is not a substitute for dominance/control dependence.
- A patch-added call's mere presence is not proof that it protects the sink.
- Bind the selected location to the unsafe call, operator, access, or deserialization site.
- Stable library/framework APIs may define scope; patch-local paths, lines, commits, labels, and
  local names may not be primary matching conditions.
