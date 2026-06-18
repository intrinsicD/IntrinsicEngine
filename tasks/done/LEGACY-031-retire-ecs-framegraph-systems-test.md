---
id: LEGACY-031
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-031 — Retire legacy ECS frame-graph systems test

## Goal
- [x] Remove the legacy `Core`/`ECS` frame-graph systems compatibility test
      from the `LEGACY-005` and `LEGACY-006` external consumer sets.

## Non-goals
- Do not promote legacy `ECS::Systems::AxisRotator`; `HARDEN-081` records it
  as demo/sample behavior rather than canonical ECS.
- Do not change promoted ECS transform, bounds, render-sync, or runtime bundle
  behavior.
- Do not delete `src/legacy/ECS/` or `src/legacy/Core/`.
- Do not resolve the broader legacy `Runtime.SystemBundles` test in this slice.

## Context
- Owner/layer: ECS/runtime test cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/ecs/Test_FrameGraphSystems.cpp` imported bare `Core` and `ECS`
  to cover the legacy transform-system registration, the legacy sample
  `AxisRotator`, and a simulated old graphics lifecycle/GPUSceneSync ordering
  chain.
- Retained promoted coverage already exists:
  `tests/unit/ecs/Test.ECS.TransformHierarchy.cpp`,
  `tests/unit/ecs/Test.ECS.BoundsPropagation.cpp`,
  `tests/unit/ecs/Test.ECS.RenderSync.cpp`, and
  `tests/contract/runtime/Test.RuntimeEcsSystemBundle.cpp` cover promoted
  transform propagation, bounds propagation, render-sync tag forwarding, and
  runtime-owned fixed-step bundle activation.
- The old graphics lifecycle ordering assertions are not migrated here because
  they belong with the broader legacy runtime/graphics system-bundle cleanup,
  not with the ECS sample-system retirement.

## Plan
- Delete the legacy frame-graph systems compatibility test.
- Remove it from the ECS test object list.
- Update legacy-removal docs and backlog notes with the reduced Core/ECS test
  consumer counts.

## Required changes
- [x] Delete `tests/unit/ecs/Test_FrameGraphSystems.cpp`.
- [x] Remove `Test_FrameGraphSystems.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced consumer counts.

## Tests
- [x] Build the affected ECS and runtime contract test targets.
- [x] Run focused promoted ECS transform/bounds/render-sync/runtime-bundle
      coverage.
- [x] Confirm the retired legacy frame-graph systems and AxisRotator test cases
      are no longer discovered.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005`, `LEGACY-006`, and `LEGACY-012` blocker notes.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `tests/unit/ecs/Test_FrameGraphSystems.cpp` no longer exists.
- [x] No test imports legacy `Core`/`ECS` only for old ECS frame-graph systems
      or the sample `AxisRotator`.
- [x] Promoted transform/bounds/render-sync/runtime-bundle tests remain wired
      and focused verification passes.
- [x] Remaining `LEGACY-005` and `LEGACY-006` test-consumer counts are current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicECSTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'ECSTransformHierarchy|ECSBoundsPropagation|ECSRenderSync|RuntimeEcsSystemBundle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R 'FrameGraphSystems|AxisRotator'
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
- Reintroducing legacy AxisRotator or system-catalog APIs under promoted ECS.
- Changing runtime fixed-step system-bundle behavior.
- Deleting or editing legacy source subtrees.
- Folding the broader `Runtime.SystemBundles` migration decision into this
  focused ECS compatibility-test retirement.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy-only/sample-system
  compatibility coverage and existing promoted ECS/runtime bundle coverage.
- No `Operational` follow-up is owed for the retired legacy AxisRotator/sample
  coverage.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 25 test consumers; the `LEGACY-006` ECS gate records 37 legacy-internal
  consumers and 23 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicECSTests IntrinsicRuntimeContractTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'ECSTransformHierarchy|ECSBoundsPropagation|ECSRenderSync|RuntimeEcsSystemBundle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 14/14 selected tests.
- `ctest --test-dir build/ci --output-on-failure -R 'ECSBoundsPropagation|ECSRenderSync|ECSTransformHierarchy' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 21/21 selected ECS tests.
- `ctest --test-dir build/ci -N -R 'FrameGraphSystems|AxisRotator'` —
  reported `Total Tests: 0` after rebuilding.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 25 test files.
- `git grep -nE '^\s*(export\s+)?import\s+ECS(\.|\b)|^\s*(export\s+)?import\s+ECS\b' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 23 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` —
  passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.
