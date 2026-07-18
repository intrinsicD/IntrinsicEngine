---
id: ARCH-015
theme: F
depends_on: []
---
# ARCH-015 — Runtime-module scope by composition and consumer cohesion

## Goal
- Decide and record when two runtime-integrated methods share one composed
  module and when they split, without inventing a family taxonomy or shared
  abstraction before a second production case exists.

## Non-goals
- No engine or method code changes.
- No predeclared clustering/registration/reconstruction/PDE module taxonomy.
- No generic clustering completion record before a real second method proves
  common semantics.
- No decision that `IRuntimeModule` must remain; `REVIEW-003` owns the
  interface right-sizing audit.

## Context
- Owner/layer: `runtime`; the decision refines ADR-0024 D1/D2/D3/D9/D12.
- The repository has exactly one production `IRuntimeModule` implementation:
  `ClusteringModule`, currently a K-Means integration. Test doubles do not
  establish a second domain consumer.
- Current output storage and completion records are K-Means-specific; the
  generic-named labels-changed reaction currently has only a K-Means producer.
  There is no DBSCAN implementation or backlog task, and the planned GMM work
  is lower-layer geometry numerics for CPD and CLOP rather than a second
  runtime clustering integration.
- The original task premise treated result shape as the deciding boundary and
  prescribed a module-family taxonomy. The repository's P1 rule requires the
  opposite direction: decide from demonstrated composition, lifetime, commit,
  and consumer cohesion.

## Status
- Completed 2026-07-18 as an accepted architecture decision; owner: Codex;
  implementation branch: `codex/arch-015-runtime-module-scope`. Commit:
  activation/right-sizing `f9681ed7`; accepted ADR and canonical docs
  `430d80e2`; adversarial-review correction `5913a9b5`.
- Grilling branches were resolved from repository evidence, as directed by the
  grilling workflow when answers are locally discoverable:
  no predeclared taxonomy; result shape is evidence rather than the boundary;
  completion DTOs stay method-specific; and `IRuntimeModule` retention remains
  open for `REVIEW-003`.
- Independent fact-checking confirmed one production `IRuntimeModule`
  implementor, no DBSCAN integration/task, and planned GMM geometry numerics
  for CPD and CLOP rather than a second runtime consumer. Adversarial review
  removed an over-broad services/execution split trigger; only independently
  owned dependency, cancellation, or commit lifecycles now force a split.

## Required changes
- [x] Record an accepted ADR that applies only after a responsibility already
      qualifies for runtime composition under ADR-0024.
- [x] Define the same-module cohesion test across app composition/lifecycle,
      durable state and world/global lifetime, compatible and co-owned
      dependency/cancellation/commit lifecycles, and published state plus
      consumer reactions.
- [x] Define objective split triggers and state that algorithm family/result
      shape alone is neither sufficient nor necessary.
- [x] Keep command, status, completion, and diagnostic records method-specific
      until a real second method demonstrates identical semantics.
- [x] Record DBSCAN, GMM, and hierarchical clustering as conditional probes,
      not predetermined module placements.
- [x] Leave `IRuntimeModule` retention/deletion explicitly to `REVIEW-003`.

## Tests
- [x] Strict documentation links and docs-sync checks pass.
- [x] Strict task policy and task-state link checks pass.
- [x] Session brief and diff hygiene checks pass.

## Docs
- [x] Add ADR-0026 and index it.
- [x] Link the decision from the architecture index, runtime architecture, and
      kernel target-state guidance.
- [x] Do not update `AGENTS.md`; the layer dependency contract is unchanged.

## Acceptance criteria
- [x] A future runtime integration has a decidable cohesion/split test without
      a speculative taxonomy.
- [x] Worked cases are conditional on demonstrated ownership and consumers.
- [x] The second-method trigger for any shared completion record is explicit.
- [x] No engine or method code changes.

## Verification
- Strict documentation links passed with 2,876 relative links checked.
- Strict docs sync, task policy, task-state links, session-brief freshness,
  root hygiene, layering, and diff hygiene passed.
- Architecture-review manual rows: runtime ownership is explicit; no dependency
  edge, backend/control surface, lifetime, concurrency, failure mode, code,
  benchmark claim, compatibility shim, or workflow change was introduced.
- Independent fact check and adversarial ADR review completed; the latter's
  single P1 finding was corrected and its focused re-review found no remaining
  actionable issue.

```bash
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode \
  --base-ref origin/main --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
```

## Forbidden changes
- Changing or ratifying the `IRuntimeModule` interface in this task.
- Implementing or promising new modules, methods, backends, or generic events.
- Treating a matching result shape as proof of shared runtime ownership.
