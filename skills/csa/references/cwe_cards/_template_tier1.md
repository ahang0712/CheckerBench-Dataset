# CWE-{{CWE_ID}}: {{CWE_NAME}}
> Tier: 1{{VARIANT_LABEL}}
> Global samples: {{GLOBAL_COUNT}} | Variant samples: {{VARIANT_COUNT}} | Source projects: {{PROJECT_COUNT}}

## 1. Typical Code Patterns

{{Fill in based on the CWE official definition, concretized for C/C++ contexts.
Describe the bug pattern as it commonly appears in real C/C++ codebases.
Use phrasing like "commonly appears when..." or "typically manifests as..."
Do NOT write "we observed in project X" or reference specific repositories.
Focus on the generic vulnerability mechanism, not project-specific details.}}

## 2. CSA Checker Architecture Recommendations

{{Recommend which ento::Checker base class and callbacks to use.
Specify the SymbolRef / ProgramState granularity needed for tracking.
Cross-reference specific patterns in references/csa_checker_patterns.md
by name (e.g., "see pattern X in csa_checker_patterns.md").
Do NOT duplicate content from csa_checker_patterns.md — just point to it.}}

## 3. Known High-Frequency False Positive Patterns

{{Based on general static analysis principles for this vulnerability class,
describe what situations commonly produce false positives.
Explain at which analysis stage to add what kind of suppression checks.
Reason from CSA static analysis first principles + inherent properties
of this vulnerability class. Do NOT reference specific project test cases.}}

## 4. Generic Discriminant Features

{{Describe how CC can quickly judge whether "the current patch is a textbook
example of this CWE" vs "a variant or hybrid". Keep this repository-agnostic —
do NOT mention specific project function names, macros, or file paths.
Repository-specific information belongs in repo_knowledge.md, not here.}}

## 5. Repository Distribution Reference

{{For non-split cards: list Top 3-5 projects where this CWE appears most
frequently, to help CC align expectations.
For kernel/userspace variant cards: describe which types of projects
this variant typically applies to.}}

---
*Tier: 1 | Generated: {{DATE}} | Source report: {{REPORT_DATE}}*
