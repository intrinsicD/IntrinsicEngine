# Process Backlog

Agentic-workflow and process-infrastructure hardening: keeping the agent
contract mirrors, task indexes, task metadata, and audit cadences mechanically
honest. These tasks change docs, task tooling, and CI policy surfaces only —
never engine code.

Origin: agentic-workflow reviews of `AGENTS.md`, `docs/agent/*`, the skill
mirrors, and the `tasks/` tree (2026-06-09 seeded PROC-001..009; 2026-07-08
seeded PROC-015..024 from a review that additionally mined the 601 retired
tasks for recurring playbooks).

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [CI-003 — Make CI gate latency observable and cancel stale runs](../../active/CI-003-ci-gate-timing-observability-and-cancellation.md)
  (currently active).
- [CI-004 — Build only the test executables selected by each gate](CI-004-label-derived-test-build-aggregates.md)
- [CI-005 — Make PR-fast a real touched-scope feedback gate](CI-005-real-touched-scope-pr-fast-gate.md)
- [CI-006 — Remove duplicate sanitizer work and calibrate test parallelism](CI-006-sanitizer-topology-and-test-parallelism.md)
- [CI-007 — Pilot persistent module-safe ccache in CI](CI-007-module-safe-persistent-ccache-pilot.md)
- [CI-008 — Reduce CTest process overhead without oversubscribing workers](CI-008-grouped-ctest-and-worker-oversubscription.md)
- [CI-009 — Route heavy gates by lifecycle and evaluate runner scaling](CI-009-heavy-gate-routing-and-runner-evaluation.md)
- [PROC-010 — Encode P1/P3/P5 research-engine invariants in AGENTS.md + review checklist](PROC-010-encode-research-engine-invariants-in-contract.md)
  (draft for owner review).
- [PROC-018 — Author the import-visibility-contract skill (playbook wave 2)](PROC-018-import-visibility-contract-skill.md)
- [PROC-019 — Author the geometry-io-format skill (playbook wave 2)](PROC-019-geometry-io-format-skill.md)
- [PROC-020 — Author the sandbox-input-lifecycle skill (playbook wave 2)](PROC-020-sandbox-input-lifecycle-skill.md)
- [PROC-021 — Wire docs-sync and task-state-link validators into CI (or retire the promise)](PROC-021-docs-sync-strict-mode-wiring.md)
- [PROC-022 — Refresh tools/* directory READMEs to match their contents](PROC-022-tool-directory-readme-refresh.md)
- [PROC-023 — Canonicalize skill-body content that outgrew its docs/agent source](PROC-023-canonicalize-skill-body-content.md)
- [PROC-024 — Give the research/method track a theme and priority](PROC-024-theme-research-method-track.md)
  (draft for owner review, scheduling half of the `PROC-010` gap).

`PROC-012` (resolve the duplicate `GEOM-027` ID by renumbering the
control-surface/KMeans backend-seam task to `GEOM-052`) is retired; see the
retirement log.

## Convergence

- These tasks anchor **Theme H — Agentic workflow hardening**.
- CI latency order: `CI-003` establishes telemetry/cancellation;
  `CI-004`, `CI-007`, and source-hotspot tasks may then proceed independently;
  `CI-005` consumes `CI-004`; `CI-006` consumes the unsanitized fast-preset
  decision from `CI-005`; `CI-008` consumes the aggregate metadata from
  `CI-004`; and `CI-009` evaluates lifecycle/runner changes only after
  `CI-003..008` have removed avoidable software duplication.
- Compile-hotspot source ownership is intentionally outside the process queue:
  `ARCH-006` owns Sandbox editor/UI and `Sandbox.cppm`, `RUNTIME-146..151` own
  `Runtime.Engine`, and `RUNTIME-152` owns `Runtime.RenderExtraction`.
- Dependency order: `PROC-001` first (every other task edits docs that are
  mirrored into skills), then `PROC-005`, `PROC-002`, and `PROC-007`
  (independent of each other), then `PROC-003`, then `PROC-004`, then
  `PROC-006`.
- `PROC-001` owns the generate-and-verify sync between `docs/agent/*` and the
  three skill mirror roots.
- `PROC-002` owns task-ID uniqueness enforcement and the ID allocation rule.
- `PROC-003` owns moving retirement history out of
  `tasks/active/README.md` and `tasks/backlog/README.md` into an append-only
  retirement log.
- `PROC-004` owns machine-readable task metadata and the generated
  `tasks/SESSION-BRIEF.md`.
- `PROC-005` owns correcting the stale "warning mode" wording in the contract.
- `PROC-006` owns surfacing lapsed audit cadences.
- `PROC-008` (done 2026-06-10) extended the state/history split and link guard to the
  per-category READMEs (follow-up opened by `PROC-003`).
- `PROC-009` (done 2026-06-22) imported the third-party `teach`,
  `grilling`, and `grill-me` productivity skills into the repo skill surface.
- `PROC-007` owns deduplicating contract restatements out of the onboarding
  prompt and giving loop mode explicit defaults and a checkpoint rule;
  `PROC-004` owns the prompt's reading-order change — the two prompt-touching
  scopes are disjoint.

Forbidden across all members: engine code changes, renaming retired task files,
weakening any check that currently runs strict in CI, and embedding
task-specific policy into `docs/agent/prompt/prompt.md`.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [PROC-001 - Skill mirror sync generator and CI gate](../../done/PROC-001-skill-mirror-sync-generator-and-ci-gate.md) (done 2026-06-09).
- [PROC-002 - Task ID uniqueness validation and allocation rule](../../done/PROC-002-task-id-uniqueness-and-allocation-rule.md) (done 2026-06-09).
- [PROC-003 - Split task index state from retirement history](../../done/PROC-003-split-task-index-state-from-retirement-history.md) (done 2026-06-09).
- [PROC-004 - Structured task front-matter and generated session brief](../../done/PROC-004-task-front-matter-and-generated-session-brief.md) (done 2026-06-09).
- [PROC-005 - Align structural-check mode text with strict CI reality](../../done/PROC-005-align-structural-check-mode-contract-text.md) (done 2026-06-09).
- [PROC-006 - Audit cadence lapse visibility](../../done/PROC-006-audit-cadence-lapse-visibility.md) (done 2026-06-09).
- [PROC-007 - Onboarding prompt tightening and loop-mode defaults](../../done/PROC-007-onboarding-prompt-tightening.md) (done 2026-06-09).
- [PROC-008 - Category README state/history split](../../done/PROC-008-category-readme-state-history-split.md)
  (done, 2026-06-10): split every category README into open lists and
  history-marked retired sections and extended
  `check_task_state_links.py` to enforce it, with the rendering DAG
  exempted explicitly.
- [PROC-009 - Import productivity skills into repo skill surface](../../done/PROC-009-import-productivity-skills.md)
  (done, 2026-06-22): imported `teach`, `grilling`, and `grill-me` from
  `mattpocock/skills`, preserved the MIT license/provenance, and documented
  their non-generated maintenance model.
- [DOCS-005 - Feature-module playbook minimal floor and config command artifact](../../done/DOCS-005-feature-module-playbook-minimal-floor.md)
  (done, 2026-06-29): added the one-caller minimal-feature floor, softened the
  full vertical-slice trigger language, and added a serializable config/command
  artifact for UI-backed feature discoverability.
- [PROC-011 - Route contract to architecture index and authoring checks](../../done/PROC-011-route-contract-to-architecture-index-and-author-checks.md)
  (done, 2026-06-29): routed `AGENTS.md` to the canonical architecture index,
  added backend-axis and config/command lane rows to the architecture checklist,
  and documented optional `## Control surfaces` and `## Backends` task sections.
- [PROC-015 - Codify recurring diagnosis playbooks as skills (wave 1)](../../done/PROC-015-diagnosis-playbook-skills-wave-1.md)
  (done, 2026-07-08): authored `intrinsicengine-vulkan-frame-triage`,
  `intrinsicengine-gpu-smoke-authoring`, and
  `intrinsicengine-stale-build-triage` from the retired-task history and
  registered them in the routing surfaces.
- [PROC-016 - Fix skills/docs mirror drift and dead routings](../../done/PROC-016-skills-docs-mirror-drift-fixes.md)
  (done, 2026-07-08): rewrote the stale skills README, mirrored
  clean-workshop/drift-audit into `intrinsicengine-review`, fixed the
  zoom-out dead route, unified the test-category taxonomy, completed
  `contract.md`'s layering table, and indexed `prompt.md` from `AGENTS.md`.
- [PROC-017 - Document branch/CI-failure/claiming/batch-seed conventions](../../done/PROC-017-workflow-convention-gaps.md)
  (done, 2026-07-08): wrote down branch naming, CI-failure→`BUG-` intake,
  task claiming, and batch-seeding ID rules in `prompt.md`,
  `task-format.md`, and `AGENTS.md` §10.
