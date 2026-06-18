---
id: LEGACY-036
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-036 — Retire legacy EventBus test

## Goal
- [x] Remove the legacy `tests/unit/core/Test_EventBus.cpp` compatibility test
      from the `LEGACY-006` and `LEGACY-010` external consumer sets.

## Non-goals
- Do not add a promoted scene dispatcher API to ECS.
- Do not promote legacy `Runtime.Selection::ApplySelection` /
  `ApplyHover` helper APIs.
- Do not change promoted selection-controller behavior or ECS event payload
  shapes.

## Context
- Owner/layer: ECS/runtime consumer-test cleanup under the `LEGACY-012`
  migration program.
- The legacy test verified `ECS::Scene::GetDispatcher()` deferred delivery for
  `SelectionChanged`, `HoverChanged`, `EntitySpawned`, `GeometryModified`,
  `GpuPickCompleted`, and `GeometryUploadFailed`, and used
  `Runtime.Selection` helper calls to enqueue selection and hover events.
- Promoted ECS owns CPU-only event payload types through `Extrinsic.ECS.Events`;
  dispatch/queueing/subscription is deliberately outside ECS per `HARDEN-063`.
- Promoted runtime owns selection mutation, hover mutation, pick readback
  interpretation, and diagnostics through `Extrinsic.Runtime.SelectionController`.
- Legacy `GpuPickCompleted` and `GeometryUploadFailed` are not promoted ECS
  events; GPU pick readback and upload failure remain runtime/graphics-owned
  diagnostics.

## Required changes
- [x] Delete `tests/unit/core/Test_EventBus.cpp`.
- [x] Remove `Test_EventBus.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced ECS and Runtime
      consumer counts.

## Tests
- [x] Build affected ECS/runtime test targets.
- [x] Run focused promoted ECS event payload and runtime selection-controller
      tests.
- [x] Confirm no legacy `EventBus` test remains registered.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the retirement
      decision.
- [x] Update `LEGACY-006`, `LEGACY-010`, and `LEGACY-012` blocker notes.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No test imports legacy `Runtime.Selection` solely to prove the old
      `Scene::GetDispatcher()` event-bus behavior.
- [x] Retained payload/selection behavior remains covered by promoted
      `ECSEvents` and `SelectionController` tests.
- [x] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
cmake --build --preset ci --target IntrinsicECSTests IntrinsicRuntimeContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -R 'ECSEvents|SelectionController|EventBus' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R 'EventBus'
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Reintroducing legacy ECS scene dispatcher behavior under promoted ECS names.
- Moving GPU pick or GPU upload failure events into the ECS layer.
- Adding compatibility re-exports from promoted runtime to legacy
  `Runtime.Selection`.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy dispatcher
  compatibility coverage and existing promoted event/selection tests.
- No `Operational` follow-up is owed for this test-consumer retirement.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-006` now records 21 remaining ECS test consumers.
- `LEGACY-010` now records 14 remaining Runtime test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicECSTests IntrinsicRuntimeContractTests IntrinsicRuntimeTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'ECSEvents|SelectionController|EventBus' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 59/59 selected promoted entries.
- `ctest --test-dir build/ci -N -R '^EventBus\.'`
  — reported `Total Tests: 0`.
- Structural checks (`check_test_layout.py`, `validate_tasks.py`,
  `check_task_policy.py`, `check_task_state_links.py`, `check_doc_links.py`,
  `generate_session_brief.py --check`, `check_docs_sync.py`, and
  `git diff --check`) — passed.
