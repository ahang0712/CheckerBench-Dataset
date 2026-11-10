# Findings format

Two modes: conversational (default) and report (`--report`). Use the right one — do not generate a full Markdown report when the user didn't ask for a file.

---

## Conversational mode (default)

Surface one finding at a time, in a single message, using this format:

```markdown
### Finding: [short descriptive title]

**CRITICAL** · confidence 9/10 · [file path with line](src/some/file.ts:42)

**What I see:** [One or two sentences describing what's in the code.]

**Why it's a problem:** [Why this is exploitable — not a list, a paragraph. Connect it to a real attack.]

**Exploit path:**
1. Attacker signs up / arrives at endpoint X.
2. They send [specific request / input].
3. System does [what].
4. Attacker now has [what — data, elevated privilege, etc.].

**Fix:** [Specific change. One or two lines of code if it's small. Point to the file if it's bigger.]

**Reference:** [OWASP category / Anthropic research / CVE number]
```

After each finding, use AskUserQuestion with these four options:

- "Walk me through the fix in more detail"
- "Explain the exploit path further"
- "Skip this one / accept the risk"
- "Save for later (append to a TODOS block)"

Move CRITICAL → HIGH → MEDIUM in strict order. Don't mix severities in a single message — one finding per turn keeps the conversation readable.

After the last finding, output a one-line summary:

> Found N CRITICAL, M HIGH, K MEDIUM. [If any CRITICAL/HIGH: "Worth running `/cso --comprehensive` as a second pass."]

Always end the review with the disclaimer from SKILL.md.

---

## Report mode (`--report`)

Write to `.security-advisor/report-YYYY-MM-DD.md`. Format:

```markdown
# Security review — [project name]

**Date:** [ISO date]
**Scope:** [diff|full|llm|deps|secrets|scope:<area>]
**Stack:** [detected stack list]
**Reviewer:** `/security-advisor` (AI-assisted, not a pentest)

## Executive summary

| Severity | Count |
|----------|-------|
| CRITICAL | N |
| HIGH     | N |
| MEDIUM   | N |

[One short paragraph: the biggest issue, the theme if there is one, and whether to run `/cso --comprehensive` for a deeper pass.]

## Attack surface map

[The map from Phase 2 — copy verbatim.]

## Findings

[One `### Finding N: title` block per finding, using the conversational format above.]

## Filter stats

- Candidates scanned: N
- Filtered by false-positive rules: M
- Filtered by confidence gate (<7/10): K
- Reported: R

## Disclaimer

[Standard disclaimer from SKILL.md]
```

Also write a machine-readable JSON sibling (`.security-advisor/report-YYYY-MM-DD.json`) with this schema:

```json
{
  "version": "1.0",
  "date": "2026-04-16T13:45:00Z",
  "scope": "diff",
  "stack": ["nextjs", "llm-apps"],
  "summary": { "critical": 0, "high": 0, "medium": 0 },
  "findings": [
    {
      "id": 1,
      "severity": "CRITICAL",
      "confidence": 9,
      "category": "Auth|LLM|Webhook|Secret|Dependency|Config|Injection|Other",
      "title": "...",
      "file": "src/...",
      "line": 42,
      "description": "...",
      "exploit_path": ["step 1", "step 2", "step 3"],
      "fix": "...",
      "reference": "OWASP A01 / LLM08 / CVE-2025-...",
      "status": "open"
    }
  ],
  "filter_stats": {
    "candidates": 0,
    "fp_filtered": 0,
    "confidence_filtered": 0,
    "reported": 0
  }
}
```

Remind the user:
- `.security-advisor/` should probably be in `.gitignore` — reports may contain sensitive details about vulnerabilities you haven't fixed yet.
- Or, if the repo is private and only reviewed internally, committing the reports creates a useful audit trail.
