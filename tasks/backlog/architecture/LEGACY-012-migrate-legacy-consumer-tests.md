---
id: LEGACY-012
theme: F
depends_on: [LEGACY-011]
---
# LEGACY-012 — Migrate legacy consumer tests to promoted coverage

## Goal
- Move or retire tests and non-legacy consumers that still import bare legacy modules after their promoted feature owners exist, so `LEGACY-001`, `LEGACY-004`, `LEGACY-005`, `LEGACY-006`, `LEGACY-008`, `LEGACY-009`, and `LEGACY-010` consumer-grep gates can become pure deletion checks.

## Non-goals
- No feature implementation in this task; missing behavior is owned by `LEGACY-011` child tasks.
- No deletion of `src/legacy/` subtrees.
- No broad test-layout rewrite unrelated to legacy imports.
- No compatibility re-export from promoted modules to legacy module names.

## Context
- Owner/layer: migration/test cleanup under the legacy retirement program.
- Current consumer-grep gates in the open `LEGACY-*` deletion tasks include `tests/**`, and several tests still import legacy module names such as `Runtime.*`, `Graphics.*`, `RHI.*`, `Core.*`, `Asset.*`, `ECS`, or `Interface`.
- This task should run after the relevant semantic replacement task retires; for example, migrate legacy runtime orchestration tests after `RUNTIME-099`, asset ingest tests after `RUNTIME-101`, and RHI/CUDA tests after `GRAPHICS-086`.
- `LEGACY-013` cleared the promoted-src bare `Core.*` imports. The remaining
  external `LEGACY-005` consumers are 21 tests after the four directly affected
  geometry API tests migrated to promoted Core types and `LEGACY-014` removed an
  unused RuntimeGraph import, and after `LEGACY-015` migrated CoreError to
  promoted Core, `LEGACY-016` migrated LogRingBuffer to promoted Core,
  `LEGACY-017` retired duplicate legacy CoreHash coverage, `LEGACY-019`
  migrated StrongHandle to promoted Core, `LEGACY-020` migrated CoreTasks to
  promoted Core, `LEGACY-021` migrated profiling/telemetry to promoted Core,
  `LEGACY-022` migrated Core frame-graph coverage to promoted Core,
  `LEGACY-023` retired legacy-only Core.Commands coverage, `LEGACY-024`
  retired legacy-only feature-catalog coverage, `LEGACY-025` retired
  legacy-only InplaceFunction coverage, `LEGACY-026` retired legacy
  Core.DAGScheduler compatibility coverage, `LEGACY-027` migrated CoreMemory
  coverage to promoted Core, `LEGACY-028` migrated Architecture SLO coverage to
  promoted Core, `LEGACY-029` retired legacy Core.Benchmark coverage while
  preserving promoted telemetry assertions, `LEGACY-030` retired duplicate
  legacy entity-command compatibility coverage, `LEGACY-031` retired legacy ECS
  frame-graph systems compatibility coverage, `LEGACY-032` retired legacy
  runtime system-bundle compatibility coverage, `LEGACY-033` retired legacy
  runtime engine-config validation coverage, and `LEGACY-034` retired legacy
  runtime frame-loop and maintenance-lane coverage, `LEGACY-035` retired legacy
  RHI deferred-destruction compatibility coverage, and `LEGACY-036` retired
  legacy EventBus dispatcher compatibility coverage, and `LEGACY-037` retired
  legacy `AssetIngestService` constructor-shape compatibility coverage. This
  task is now the external cleanup owner for the remaining Core gate as
  well as the other subtree gates.
- `LEGACY-018` retired the only external `LEGACY-001` test consumer,
  `tests/contract/ui/Test_PanelRegistration.cpp`, because `Interface::GUI`
  panel registration is not a promoted endpoint. `LEGACY-001` now has zero
  external test consumers and remains blocked only by six legacy-internal
  Graphics/Runtime consumers.

## Value gate
- Current state: tests still keep legacy subtree deletion gates red even when promoted equivalents may already exist; promoted source no longer imports bare legacy module names after `LEGACY-013`.
- Improvement: tests validate promoted contracts and deletion tasks become mechanical consumer-grep checks.
- Scope decision: migrate only coverage that maps to retained promoted behavior. Delete legacy-only tests when the value-gated parent map records a retirement decision.

## Required changes
- [ ] Inventory every non-`src/legacy/**` import of bare legacy modules with the consumer-grep commands from the open `LEGACY-*` deletion tasks.
- [ ] Classify each consumer as promoted-equivalent coverage, legacy-only coverage to delete, or coverage blocked by a named `LEGACY-011` child task.
- [ ] Migrate promoted-equivalent tests to `Extrinsic.*` modules and current test labels/locations.
- [ ] Delete legacy-only tests only after recording why the behavior is retired or covered elsewhere.
- [ ] Update each affected `LEGACY-*` deletion task with the remaining consumer-grep blockers.

## Tests
- [ ] `python3 tools/repo/check_test_layout.py --root . --strict` passes after moved/deleted tests.
- [ ] The focused migrated test targets pass under the default CPU/null gate labels.
- [ ] Each affected `LEGACY-*` consumer-grep gate either exits 0 or reports only blockers tied to named open feature tasks.

## Docs
- [ ] Update `docs/migration/legacy-retirement.md` with migrated/deleted consumer-test status.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` if a legacy-only test is deleted because behavior is formally retired.
- [ ] Update `tests/README.md` only if test labels or layout policy changes.

## Acceptance criteria
- [ ] No test imports a legacy module name when an equivalent promoted contract exists.
- [ ] Remaining legacy test imports are explicitly blocked by named feature tasks or deleted with documented retirement rationale.
- [ ] Open legacy deletion tasks have current consumer-grep blocker notes.

## Verification
```bash
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Run the consumer-grep gates copied from the affected LEGACY-* deletion tasks.
# Run focused CTest filters for every migrated test family.
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding compatibility re-exports from promoted modules to legacy names.
- Removing legacy coverage before its promoted replacement or explicit retirement decision exists.

## Maturity
- Target: `Retired` for legacy consumer-test imports.
- No `Operational` follow-up is owed for test-consumer migration.
