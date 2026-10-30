---
id: ARCH-022
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: program tracker; evidence lives in each slice task and the target's measured rules
contract_schema: 1
contracts: []
contract_review: tracker only; each slice declares its own contracts
---
# ARCH-022 — Engine and feature modules: the architecture target

## Operator questions (verbatim, 2026-10-30)
34. "Ok ich bin etwas verwirrt… Ich habe das Gefühl dass wir seit ewigkeiten an der Architektur
    herumschrauben. Eigentlich hatte ich immer versucht IntrinsicEngine so zu bauen dass Änderungen
    leicht sind, die Architektur modular und die Abhängigkeiten gering. Nun scheint es so dass alles
    wieder total kompliziert zugewuchert ist? Die Engine soll eigentlich das Fundament bilden und
    Module/Plugins fügen Features und Methoden hinzu. Ich weiß nicht an welcher Stelle wir falsch
    abgebogen sind."
35. "Ich will 2. machen. Kannst du hier etwas vorschlagen und ich schaue mir das dann an?" (point 2:
    a one-page target picture and an inventory, no code)
36. "Also ich hätte gerne alle alten zieldokumente archiviert. Dann nehmen wir nur dieses hier als
    aktuelles und das wird erst ersetzt wenn die Ziele erfüllt sind. Ja, jedes Modul/Plugin meldet
    sich selbst an. Ich will auch Laufzeit Kompilierung damit Agenten etwas ändern kannst du die
    sandbox/engine weiterlaufen kann. Außerdem sollen Agenten die Engine komplett per mcp steuern
    können. Wenn wir für dieses Ziel Dinge umbauen müssen dann ist das ok. Am Ende zählt das
    Änderungen günstig sind, die Engine modular und erweiterbar ist und dass der Build und ci nicht
    so lange dauert."

## Goal
- Reach the target in
  [`docs/architecture/engine-and-feature-modules.md`](../../docs/architecture/engine-and-feature-modules.md):
  a foundation, a small set of module services, and self-registering feature plugins that are
  rebuilt and reloaded while the Sandbox runs and are fully controllable over MCP. Measured by its
  rules R1–R6. This note says where the work is and what comes next; each slice has its own task.

## State
- 2026-10-30: the target was written and accepted by the operator.
  - All earlier target documents are archived (`docs/archive/README.md`).
  - ADR 0031 is superseded; the moonshot program (`ARCH-020`) is closed by this decision.
- **Next:** the first plan, for the operator's review. It fixes the order of steps (the target's
  open points) and the numeric targets for R4 and R6 once their baselines are measured. The
  proposed first slices are:
  1. baselines plus an R1/R2 checker;
  2. the hot-reload spike;
  3. registries;
  4. moving the features out of the editor.

## Acceptance criteria
- [ ] Rules R1–R6 of the target meet their targets, as measured by the checker and the baselines
      (the operator decides; this replaces the target page only then).

## Verification
```bash
python3 tools/docs/check_doc_links.py --root . --strict
```

## Log
- 2026-10-30: Q34 answered with a diagnosis: the kernel is slim and enforced, but the methods never
  got a boundary. Q35: a proposal written (the three rings, rules R1–R3 with today's numbers, the
  inventory). Q36: decisions recorded in the target. Rules R4–R6 were added with today's values:
  - CI `ci-linux-clang` median 40 min over 36 runs;
  - 48 MCP tools from a hand-written list;
  - hot reload not possible.

  13 target documents were archived, with links rewritten across 52 files (strict link check
  clean). A cleanup of the 45 active tasks, many of them finished M2 and ADR 0031 slices, is
  proposed as its own step.
- 2026-10-30: Frozen task records may not be edited, so the link rewrite there was undone. Four
  archived documents keep a forwarding page at their old path. The docs CI checks pass locally in
  strict mode: links, task policy, task-state links, docs sync, root hygiene, skill mirrors and claims.
- 2026-10-30: Codex review of the staged change (`review022`, fresh session): revise.
  - R1's measure covered only the 13 files naming five or more features. It is now scoped to
    `src/runtime` and `src/app`: 31 files name two or more (13 of them five or more), and 4 files in
    `src/geometry` are counted apart.
  - Three import counts were corrected: 42, 38 and 25.
  - `roadmap.md`'s architecture sequencing was archived, and ADR 0024's "living scorecard" line
    corrected.
  - The no-framework, no-golden and three-plugin rules are marked as proposals, not operator
    decisions.
  - The kernel stays under its convergence checker.
- 2026-10-30: Operator (Q37): "Bitte räum erst auf." The 31 tasks of the moonshot M2 and ADR 0031
  programs were retired to `tasks/done/`. Each has a retirement line with its commit reference and
  maturity, and an entry in `RETIREMENT-LOG.md`.
  - RUNTIME-354's three criteria were recorded as met, with their review sources.
  - ARCH-020's two criteria were dropped, unmet, by the operator's decision.
  - ARCH-021 and ADR 0031 are superseded.

  The session brief was regenerated, and the links were retargeted (frozen records only in their
  directory part). Evidence:
  - RUNTIME-354's scripts move to `tasks/evidence/ARCH-022/` (this task still uses them);
  - RUNTIME-326's logs and the RUNTIME-351 and RUNTIME-353 scripts are proposed for deletion, since
    the reports reproduce them.

  13 older active tasks with open points remain (September and October; three with stale `codex`
  claims). Whether they move to the backlog is put to the operator.
