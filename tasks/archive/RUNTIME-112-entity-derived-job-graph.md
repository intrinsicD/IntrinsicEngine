---
id: RUNTIME-112
theme: F
depends_on: [RUNTIME-111]
maturity_target: CPUContracted
---
# RUNTIME-112 — Entity derived-job graph and snapshots

## Completion
- Completed: 2026-06-16. Commit/PR: this retirement commit.
- Maturity: `CPUContracted`.
- Fix summary: added `Extrinsic.Runtime.DerivedJobGraph`, a
  `StreamingExecutor`-backed, entity-scoped derived-job registry with stable
  keys, dependencies, per-entity/global snapshots, stale/cancel handling,
  previous-output retention, follow-up scheduling, main-thread apply, and
  deterministic fail-closed GPU-domain diagnostics.
- Evidence: focused runtime contract tests exercise dependency order,
  snapshots, stale generation checks, follow-up edges, cancellation/delete,
  failure retention, and unsupported GPU job-domain metadata.
- Follow-up boundary: concrete import/extraction/UI consumers are owned by
  `RUNTIME-114`, `RUNTIME-113`, and `UI-015`; no separate operational backend
  follow-up is owed for the scheduler contract itself.

## Goal
- Add a runtime-owned entity-scoped derived-job graph over
  `StreamingExecutor`, with dependency snapshots, progress/debug visibility,
  stale-result discard, and follow-up scheduling.

## Non-goals
- No mesh/graph/point-cloud packing or bake algorithms beyond small test
  doubles.
- No UI rendering widgets; this task exposes data-only snapshots.
- No GPU compute implementation.
- No graphics resource allocation or upload ownership.
- No persistence of transient job state.

## Context
- Owning subsystem/layer: `runtime` owns async derived-work orchestration,
  main-thread apply, and ECS/source-generation validation.
- ADR-0021 requires jobs keyed by stable entity id, geometry domain, source
  generation, and output semantic. Jobs may depend on other jobs and may
  schedule follow-up jobs while preserving explicit dependency edges.
- `RUNTIME-111` supplies descriptor identity and readiness vocabulary consumed
  by this scheduler.
- The current `StreamingExecutor` is the promoted async runtime path; this task
  must not revive the retired `TaskGraph` bridge.

## Required changes
- [x] Add a data-only `DerivedJobKey` and `DerivedJobRecord` model for entity
      id, geometry domain, source generation, output semantic, requested job
      domain, dependencies, progress, status, diagnostic text, and timing.
- [x] Add a runtime registry that schedules CPU jobs on `StreamingExecutor`
      and exposes deterministic snapshots for selected-entity and global queue
      views.
- [x] Add dependency tracking so blocked, queued, running, applying, complete,
      failed, cancelled, and stale/discarded states are explainable from
      snapshots.
- [x] Add main-thread apply hooks that validate entity, geometry generation,
      source property generation, and binding generation before mutating
      runtime/ECS state.
- [x] Add stale-result discard and best-effort cancellation behavior for source
      changes, entity deletion, and binding changes.
- [x] Add follow-up scheduling support where a completed job can enqueue
      dependent work and record the explicit dependency edge that caused it.
- [x] Add previous-output retention policy so failures or in-progress
      replacements leave the last valid output bound until replacement output
      is ready.
- [x] Add deterministic unavailable diagnostics for `GpuCompute`,
      `GpuGraphics`, and unsupported `Auto` resolutions until GPU domains gain
      concrete backend tasks.

## Tests
- [x] Add CPU/null tests for job dependency ordering and snapshot status
      transitions.
- [x] Add stale-result discard tests for changed entity generation, geometry
      generation, source property generation, and binding generation.
- [x] Add follow-up scheduling tests proving newly queued work records the
      dependency edge from the completed job.
- [x] Add failure tests proving previous valid output remains active and
      diagnostics are visible.
- [x] Add cancellation/delete tests proving dead entities do not receive
      completed worker output.
- [x] Add GPU-domain metadata tests proving unsupported GPU jobs fail closed
      with deterministic diagnostics.

## Docs
- [x] Update `src/runtime/README.md` with derived-job lifecycle, snapshot
      status taxonomy, stale apply rules, and `StreamingExecutor` ownership.
- [x] Link the job graph documentation to ADR-0021.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface
      changes.

## Acceptance criteria
- [x] Runtime can expose per-entity and global derived-job snapshots without UI
      or graphics ownership.
- [x] Jobs can express dependencies and schedule follow-up work while preserving
      explicit graph edges.
- [x] Stale or cancelled completions cannot mutate current entity data.
- [x] Failures retain previous valid outputs and report diagnostics.
- [x] The default CPU-supported CTest gate verifies the job graph contract.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'DerivedJob|StreamingExecutor|Progressive' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not restore or depend on the retired `Engine::GetStreamingGraph()` /
  `TaskGraph` bridge.
- Do not apply worker results off the main thread.
- Do not persist transient job records in scene files.
- Do not allocate graphics, Vulkan, or RHI resources from the scheduler.
- Do not hide follow-up work behind implicit, unobservable callbacks.

## Maturity
- Target: `CPUContracted`.
- This task closes the derived-job scheduling and snapshot contract. For the
  scheduling contract itself, no Operational follow-up is owed; integration into
  progressive import/render workflows is owned by `RUNTIME-114` and `UI-015`.
