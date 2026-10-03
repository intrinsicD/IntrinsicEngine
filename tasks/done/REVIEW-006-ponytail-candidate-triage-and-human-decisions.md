---
id: REVIEW-006
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive review planning requested by the operator; candidate dossiers, separate Codex and Claude reviews, and explicit human decisions are retained in this task. No implementation or research result is authorized by this task.
contract_schema: 1
contracts: []
contract_review: Reviewed the contract catalog. This task records and examines cleanup hypotheses without changing engine contracts, source interfaces, tests, formats, or repository-wide workflow policy. The backlog README receives only a navigation link. Each approved implementation task must independently discover and declare its applicable contracts before implementation.
---
# REVIEW-006 — Ponytail-Funde zerlegen und einzeln mit Mensch, Codex und Claude entscheiden

## Goal

Planungsnotiz für die Triage des Ponytail-Audits vom 2026-10-03 (Revision
`0ebb2f450`). Auf Operator-Wunsch mit der parallel angelegten Notiz in
[REVIEW-007](../backlog/architecture/REVIEW-007-ponytail-audit-triage.md)
zusammengeführt und als ersetzte Planungsnotiz retired.

## Acceptance criteria

- [x] Inhalt vollständig nach REVIEW-007 übernommen: Codex-Audit: zwölf Fundgruppen PK01–PK12 sowie Prozess, Autorisierungsgrenzen, Zerlegungsregeln und Dossierformat.
- [x] Keine Kandidatenprüfung, Entscheidung, Umsetzung oder Codeänderung
  unter dieser Notiz; alle offenen Punkte gehören REVIEW-007.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```

## Completion

- Retired 2026-10-03 als ersetzte Planungsnotiz (superseded by REVIEW-007).
  Die Notiz wurde vor dem Zusammenführen nie committet; ihr vollständiger
  Inhalt lebt in REVIEW-007 weiter.
- Commit reference: der Commit, der REVIEW-007 einführt.
