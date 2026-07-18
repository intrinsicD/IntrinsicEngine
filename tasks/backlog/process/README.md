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

- [PROC-025 — Port the research-ideation skill to structsplat and prospect at IntrinsicEngine-parity quality](../../active/PROC-025-port-research-ideation-skill-to-other-repos.md)
  is active. Both repo-native skills already landed; the designated target
  branches now refresh their research frontier and verify structural parity.

`PROC-012` (resolve the duplicate `GEOM-027` ID by renumbering the
control-surface/KMeans backend-seam task to `GEOM-052`) is retired; see the
retirement log.

## Convergence

- These tasks anchor **Theme H — Agentic workflow hardening**.
- CI latency order: retired `CI-003` establishes telemetry/cancellation and
  retired `CI-004` establishes label-derived test build aggregates; retired
  `CI-007` establishes the bounded module-safe `pr-fast` ccache policy;
  retired `BUG-107` makes the configured target graph deterministic, and
  retired `BUG-106` restores truthful test ownership. Retired `CI-010`
  establishes coverage parity; retired `CI-005` consumes the corrected
  registry/graph; retired `CI-006` isolates the retained sanitizer variants;
  retired `CI-011` establishes the measured fast/slow cohort; and retired
  `CI-008` now groups five audited pure producers, retains local individual
  discovery, and fixes the required full-CPU worker budget at four.
  Retired `BUILD-004` independently repairs compile-hotspot evidence and
  unblocks `RUNTIME-166`. Retired `BUG-114` repairs the Release SLO
  workload/metric contract, and retired `CI-009` now owns the final
  quick-feedback, merge-confidence, optimized-Release, coverage-lifecycle, and
  standard-runner decision after the software duplication and evidence
  defects were removed.
- Compile-hotspot analyzer ownership is retired `BUILD-004`; source
  optimization stays outside the process queue. `ARCH-006` retired the Sandbox
  editor/UI hotspot, `RUNTIME-146..151` own the historical `Runtime.Engine`
  decomposition, and `RUNTIME-166` owns `Runtime.RenderExtraction`.
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
task-specific policy into `docs/agent/prompt/prompt.md`. The sole exception in
this batch is the evidence-backed `check_pr_contract.py` retirement audited by
`PROC-027`.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [CI-009 — Route heavy gates by lifecycle and evaluate runner scaling](../../done/CI-009-heavy-gate-routing-and-runner-evaluation.md)
  (done 2026-07-18): separated quick feedback from fail-closed candidate
  confidence, promoted an unsanitized optimized Release SLO/benchmark lane,
  scheduled complete CPU source coverage, and retained `ubuntu-24.04` after
  five unchanged-SHA runs measured 2/3-second queue median/p95 and
  445/478-second optimized-job median/p95. No comparable larger runner was
  registered, so the A/B remains deferred behind quantified reopen, cost,
  adoption, and rollback criteria.
- [BUILD-004 — Make compile-hotspot evidence source-complete](../../done/BUILD-004-source-complete-compile-hotspot-evidence.md)
  (done 2026-07-18): normalized physical Ninja compile edges across all
  repository-owned C++ roots, calibrated five exact edge baselines from five
  clean hosted samples, and proved the normal CPU workflow reports all 4,062
  logical correctness cases before enforcing the hotspot gate.
- [CI-008 — Reduce CTest process overhead without oversubscribing workers](../../done/CI-008-grouped-ctest-and-worker-oversubscription.md)
  (done 2026-07-18): replaced 1,351 individual cases from five audited pure
  producers with five grouped wrappers in required CPU variants, reduced
  physical records from 4,062 to 2,716, proved exact logical/coverage parity,
  and retained the fastest absolute grouped plan at `--parallel 4`.
- [CI-011 — Calibrate the slow-test cohort and retain fast sentinels](../../done/CI-011-measured-slow-test-cohort.md)
  (done 2026-07-17): moved exactly eight measured heavy cases to the scheduled
  ordinary-slow lane, retained eight fast sentinels, reduced comparable
  PR-fast median/p95 by 30.565%/29.357%, and proved zero lost source regions or
  branch arms.
- [CI-006 — Remove duplicate sanitizer work and isolate variants](../../done/CI-006-sanitizer-topology-and-test-parallelism.md)
  (done 2026-07-17): isolated unsanitized, ASan, UBSan, and promoted-Vulkan
  identities, proved exact 4,062-case parity across the three CPU variants,
  and retained the first five-sample identical-selector timing baseline
  without making a causal speedup claim.
- [CI-005 — Make PR-fast a real touched-scope feedback gate](../../done/CI-005-real-touched-scope-pr-fast-gate.md)
  (done 2026-07-17): added a fail-closed staged touched-scope planner and
  unsanitized `ci-fast` route, retained five-sample docs/focused/broad timing
  evidence, and rejected focused cross-layer smoke after its configured
  closure exceeded the declared budget.
- [CI-010 — Establish CPU source-coverage refactor parity](../../done/CI-010-cpu-source-coverage-refactor-parity.md)
  (done 2026-07-17): added a complete Clang CPU source-coverage baseline,
  exact producer/profile/object reconciliation, and schema-v2 refactor parity
  that permits test-target splits but fails on lost production regions,
  branches, or case working-directory identity.
- [PROC-027 — Audit the validator/tool fleet for rent-paying gates](../../done/PROC-027-validator-rent-audit.md)
  (done 2026-07-17): inventoried all 57 snapshot Python tools plus 16
  non-Python wrappers, retained 53, kept 2 report-only, and retired 2
  evidence-free tools with every live caller and stale documentation reference.
- [DOCS-006 — Curated "how this repo is built" agentic-development narrative](../../done/DOCS-006-agentic-development-narrative.md)
  (done 2026-07-17): authored the outside-reader tour of the task lifecycle,
  convergence themes, skill and validator tiers, and CI gates, grounded in the
  complete `CI-004` seed-to-archive evidence trail.
- [CI-007 — Pilot persistent module-safe ccache in CI](../../archive/CI-007-module-safe-persistent-ccache-pilot.md)
  (done 2026-07-13): retained the bounded `pr-fast` ccache store after five
  cold and five warm hosted samples, zero cache errors, 56.7% lower build
  median, 58.3% lower build p95, and clean/interface-change parity.
- [PROC-024 — Give the research/method track a theme and priority](../../archive/PROC-024-theme-research-method-track.md)
  (done 2026-07-11): created **Theme I — Research method implementation (P1)** with
  the 16 open `METHOD-*`/`GEOM-*` members, so the research mission is a
  first-class picker target instead of `Unthemed`.
- [PROC-010 — Encode P1/P3/P5 research-engine invariants in AGENTS.md + review checklist](../../archive/PROC-010-encode-research-engine-invariants-in-contract.md)
  (done 2026-07-11): promoted P1 (research pragmatism), P3 (config-lane control
  surface), and P5 (recipe-driven frames) into always-on `AGENTS.md` §5 invariants
  with matching per-PR review rows; the stale Theme I proposal (section C) was
  dropped since its members had retired.
- [PROC-020 — Author the sandbox-input-lifecycle skill (playbook wave 2)](../../archive/PROC-020-sandbox-input-lifecycle-skill.md)
  (done 2026-07-11): authored the SKILL.md-only
  `intrinsicengine-sandbox-input-lifecycle` discipline skill capturing six
  runtime frame-loop wiring pitfalls (each citing its evidencing retired bug),
  registered in the `intrinsicengine-core` routing table and the skills
  `README.md` discipline tier.
- [PROC-019 — Author the geometry-io-format skill (playbook wave 2)](../../archive/PROC-019-geometry-io-format-skill.md)
  (done 2026-07-11): authored the SKILL.md-only
  `intrinsicengine-geometry-io-format` discipline skill distilling the GEOIO-002
  importer/exporter slice shape (verified against `GEOIO-002B`/`002D`/`002E` and
  the live IO surface), registered in the `intrinsicengine-core` routing table
  and the skills `README.md` discipline tier.
- [PROC-018 — Author the import-visibility-contract skill (playbook wave 2)](../../archive/PROC-018-import-visibility-contract-skill.md)
  (done 2026-07-11): authored the SKILL.md-only
  `intrinsicengine-import-visibility-contract` discipline skill (seven-item
  checklist, each item citing its evidencing retired import/visibility bug),
  registered in the `intrinsicengine-core` routing table and the skills
  `README.md` discipline tier.
- [PROC-023 — Canonicalize skill-body content that outgrew its docs/agent source](../../archive/PROC-023-canonicalize-skill-body-content.md)
  (done 2026-07-11): classified the `intrinsicengine-benchmark`, `-method`, and
  `-docs-sync` skill bodies section-by-section and declared their only-here
  sections (`Anti-patterns`; knowledge-graph aid + maturity mapping; `Decision
  rules for common cases`) skill-canonical, with an `Authority (PROC-023)` note
  in each body and a per-skill model table in the skills `README.md`.
- [PROC-022 — Refresh tools/* directory READMEs to match their contents](../../archive/PROC-022-tool-directory-readme-refresh.md)
  (done 2026-07-11): reconciled `tools/agents`, `tools/ci`, and `tools/repo`
  READMEs to factual current state — every script/config listed with purpose
  and CI wiring — and removed stale `Planned moves` / `Compatibility
  entrypoints` sections referencing the retired RORG-041/071/112 tasks.
- [PROC-021 — Wire docs-sync and task-state-link validators into CI](../../archive/PROC-021-docs-sync-strict-mode-wiring.md)
  (done 2026-07-10): `ci-docs` now enforces strict PR-diff docs synchronization
  and strict task-state links, with full base history, static workflow coverage,
  current policy docs, and a fresh generated skill reference.
- [CI-004 — Build only the test executables selected by each gate](../../archive/CI-004-label-derived-test-build-aggregates.md)
  (done 2026-07-10): derived gate-specific aggregates from canonical test-label
  metadata, routed PR-fast and Vulkan workflows to exact executable closures,
  and verified hosted edge/time deltas without claiming a PR-fast speedup.
- [CI-003 — Make CI gate latency observable and cancel stale runs](../../archive/CI-003-ci-gate-timing-observability-and-cancellation.md)
  (done 2026-07-09): added the stable per-run timing profile, instrumented and
  cancellation-scoped every compile-heavy workflow, and published the
  API-verified five-sample-per-gate aggregate baseline.
- [PROC-001 - Skill mirror sync generator and CI gate](../../archive/PROC-001-skill-mirror-sync-generator-and-ci-gate.md) (done 2026-06-09).
- [PROC-002 - Task ID uniqueness validation and allocation rule](../../archive/PROC-002-task-id-uniqueness-and-allocation-rule.md) (done 2026-06-09).
- [PROC-003 - Split task index state from retirement history](../../archive/PROC-003-split-task-index-state-from-retirement-history.md) (done 2026-06-09).
- [PROC-004 - Structured task front-matter and generated session brief](../../archive/PROC-004-task-front-matter-and-generated-session-brief.md) (done 2026-06-09).
- [PROC-005 - Align structural-check mode text with strict CI reality](../../archive/PROC-005-align-structural-check-mode-contract-text.md) (done 2026-06-09).
- [PROC-006 - Audit cadence lapse visibility](../../archive/PROC-006-audit-cadence-lapse-visibility.md) (done 2026-06-09).
- [PROC-007 - Onboarding prompt tightening and loop-mode defaults](../../archive/PROC-007-onboarding-prompt-tightening.md) (done 2026-06-09).
- [PROC-008 - Category README state/history split](../../archive/PROC-008-category-readme-state-history-split.md)
  (done, 2026-06-10): split every category README into open lists and
  history-marked retired sections and extended
  `check_task_state_links.py` to enforce it, with the rendering DAG
  exempted explicitly.
- [PROC-009 - Import productivity skills into repo skill surface](../../archive/PROC-009-import-productivity-skills.md)
  (done, 2026-06-22): imported `teach`, `grilling`, and `grill-me` from
  `mattpocock/skills`, preserved the MIT license/provenance, and documented
  their non-generated maintenance model.
- [DOCS-005 - Feature-module playbook minimal floor and config command artifact](../../archive/DOCS-005-feature-module-playbook-minimal-floor.md)
  (done, 2026-06-29): added the one-caller minimal-feature floor, softened the
  full vertical-slice trigger language, and added a serializable config/command
  artifact for UI-backed feature discoverability.
- [PROC-011 - Route contract to architecture index and authoring checks](../../archive/PROC-011-route-contract-to-architecture-index-and-author-checks.md)
  (done, 2026-06-29): routed `AGENTS.md` to the canonical architecture index,
  added backend-axis and config/command lane rows to the architecture checklist,
  and documented optional `## Control surfaces` and `## Backends` task sections.
- [PROC-015 - Codify recurring diagnosis playbooks as skills (wave 1)](../../archive/PROC-015-diagnosis-playbook-skills-wave-1.md)
  (done, 2026-07-08): authored `intrinsicengine-vulkan-frame-triage`,
  `intrinsicengine-gpu-smoke-authoring`, and
  `intrinsicengine-stale-build-triage` from the retired-task history and
  registered them in the routing surfaces.
- [PROC-016 - Fix skills/docs mirror drift and dead routings](../../archive/PROC-016-skills-docs-mirror-drift-fixes.md)
  (done, 2026-07-08): rewrote the stale skills README, mirrored
  clean-workshop/drift-audit into `intrinsicengine-review`, fixed the
  zoom-out dead route, unified the test-category taxonomy, completed
  `contract.md`'s layering table, and indexed `prompt.md` from `AGENTS.md`.
- [PROC-017 - Document branch/CI-failure/claiming/batch-seed conventions](../../archive/PROC-017-workflow-convention-gaps.md)
  (done, 2026-07-08): wrote down branch naming, CI-failure→`BUG-` intake,
  task claiming, and batch-seeding ID rules in `prompt.md`,
  `task-format.md`, and `AGENTS.md` §10.
