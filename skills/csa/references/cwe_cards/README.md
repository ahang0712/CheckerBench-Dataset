# CWE Skills — Per-Category Vulnerability Knowledge for CSA Checker Synthesis

## Purpose

When CC synthesizes a checker for a CVE, it knows *what the patch fixes* but not
*what this class of vulnerability typically looks like in C/C++ code*, nor *what
checker architecture tends to work best for detecting it*.

A *CWE skill* is a pre-generated knowledge card that captures this domain-level
understanding **once per CWE category** and injects it into every synthesis task
whose CVE carries that CWE label. The card is copied into
`workdir/references/cwe_cards/` alongside `repo_knowledge.md`.

## Relationship to repo_knowledge.md

| Dimension | `repo_knowledge.md` | CWE skill |
|-----------|-----------------|-----------|
| Axis | Per-repository | Per-vulnerability-class |
| Answers | "What are this project's alloc/free conventions?" | "What does a typical UAF bug look like?" |
| Updated | Incrementally after each synthesis | Static (regenerated when distribution changes) |
| Source | CC explores repo source code | CC uses CWE official definition + CSA domain knowledge |

Both files coexist under `references/` and complement each other.

## Tier System

CWEs are stratified into 3 tiers based on sample count in the pending pairs dataset:

| Tier | Threshold | Card Structure | Count |
|------|-----------|---------------|-------|
| **Tier 1** | N ≥ 100 | Full 5-section card, kernel/userspace variants if Linux/Non-Linux ratio ≥ 2.0 | 7 CWEs → 13 cards |
| **Tier 2** | 20 ≤ N < 100 | Condensed 4-section card, single file | 16 CWEs → 16 cards |
| **Tier 3** | N < 20 | Umbrella card grouping multiple CWEs by category | 88 CWEs → 8 umbrella cards |

Special handling for `NVD-CWE-noinfo` and `NVD-CWE-Other`:
`CWE-unclassified-diagnosis.md` guides CC to infer the vulnerability type from
the patch diff itself.

## File Structure

```
cve_checker_synth/prompts/cwe_skills/
├── README.md                          ← this file
├── _template_tier1.md                 ← Tier 1 card template (CC fills this)
├── _template_tier2.md                 ← Tier 2 card template
├── _template_tier3_umbrella.md        ← Tier 3 umbrella template
├── CWE-unclassified-diagnosis.md      ← static diagnostic guide
├── index.json                         ← lookup index (generated)
├── .classification_result.json        ← intermediate tier data (generated)
├── .cwe_definitions/                  ← cached mitre.org HTML (pre-fetched)
├── CWE-{id}(-{variant}).md            ← generated cards (Tier 1/2)
└── Category-{name}.md                 ← generated umbrella cards (Tier 3)
```

## Lookup Logic (at synthesis time)

For a given CVE pair with CWE label `{cwe_id}`:

1. If `{cwe_id}` is `NVD-CWE-noinfo` or `NVD-CWE-Other`:
   → Copy `CWE-unclassified-diagnosis.md`
2. Look up `{cwe_id}` in `index.json`
3. If Tier 1 with `has_repo_variant == true`:
   → If `project_id` is in `kernel_repo_match`: copy `{cwe_id}-kernel.md`
   → Otherwise: copy `{cwe_id}-userspace.md`
4. If Tier 1 (no variant) or Tier 2:
   → Copy `{card_path}`
5. If Tier 3:
   → Copy umbrella `{card_path}`
6. If CWE not in index:
   → Skip silently (new CWE, will be classified in next refresh)

## Regeneration

When the dataset grows significantly (new pending pairs added), re-run:

```bash
python3 scripts/generate_cwe_skill.py --tier-only   # review classification
python3 scripts/generate_cwe_skill.py --generate     # save classification
python3 scripts/generate_cwe_skill_cc.py             # regenerate all cards
```

The old cards are overwritten. Merge any valuable accumulated knowledge
before regeneration.

## Verification

After generation, verify:
1. `index.json` has entries for all 111 regular CWEs
2. Pick 2-3 CVE pairs with different CWEs, simulate lookup in GenericWorkdirPreparer
3. Check that Tier 1 split CWEs resolve correctly for torvalds_linux vs other projects
