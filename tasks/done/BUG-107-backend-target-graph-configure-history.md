---
id: BUG-107
theme: G
depends_on: []
---
# BUG-107 — Backend target graph depends on configure history

## Status
- Completed on 2026-07-17 at `Retired`; owner: Codex; branch: `main` (local).
- Commit reference: this retirement commit contains the implementation and
  lifecycle closure.
- Root-owned build identity now exists before every renderer/platform
  consumer. The deterministic configure regression passed for explicit
  Null/headless, explicit Vulkan/Glfw, and supported Auto configurations.
- Fresh `ci` and `ci-vulkan` configure/build verification passed; the default
  CPU-supported CTest gate passed 3,830/3,830.

## Goal
- Make backend and platform target selection deterministic on the first clean
  configure and every same-input reconfigure.

## Non-goals
- No new graphics backend, backend registry, factory, service, or runtime
  selection policy.
- No change to the promoted Vulkan opt-in truth table or the documented
  `Auto|Null|Glfw` platform choices.
- No test capability relabeling; that is `BUG-106`.
- No broad CMake source-tree reorganization.

## Context
- Symptom: the top-level build adds `src/graphics/renderer` before
  `src/platform`. `src/graphics/renderer/Backends/CMakeLists.txt` reads
  `EXTRINSIC_BACKEND`, while `src/platform/CMakeLists.txt` establishes its
  default only later.
- Symptom: `ci-vulkan` pins `EXTRINSIC_BACKEND=Vulkan`, but the base/`ci`
  presets do not. A clean configure can therefore evaluate the renderer with
  an undefined value, then leave a cached default that changes a later
  reconfigure's target graph.
- Expected behavior: global backend inputs exist before their first consumer,
  and identical preset/source inputs produce identical target and test
  inventories regardless of build-tree history.
- Impact: focused and coverage gates can silently build different backend/test
  sets on clean versus reused trees, invalidating selection and timing evidence.
- Owner: root CMake configuration and platform/renderer build wiring. This is
  a build-graph correction, not an engine-layer dependency change.
- Reproduced on 2026-07-17: two independent fresh `ci` trees each registered
  26 test targets and 25 initial CPU CTest entries; an unchanged reconfigure
  of either tree registered 32 targets and 30 entries by adding the Vulkan
  target family. The second fresh configure matched the first, isolating
  configure history rather than source or preset drift.

## Required changes
- [x] Establish `EXTRINSIC_PLATFORM` and `EXTRINSIC_BACKEND` defaults in one
      top-level configuration location before any consuming subdirectory is
      added; remove later ownership of those global defaults from `platform`.
- [x] Pin the intended backend/platform identity in current CI presets without
      changing their tested capability cohort. `CI-005` separately owns the
      future explicit Null/headless `ci-fast` identity.
- [x] Preserve the existing `INTRINSIC_PLATFORM_BACKEND=Auto|Null|Glfw`
      resolution and promoted Vulkan runtime opt-in rules.
- [x] Emit the resolved backend/platform identity once in configure output and
      make timing/result metadata consume that same identity where available.
- [x] Add a small deterministic inventory comparison; do not introduce a
      general CMake File-API dependency planner or backend-registration layer.

## Tests
- [x] Add `tests/regression/tooling/Test.BackendConfigureDeterminism.py` that
      compares a first fresh configure, same-tree reconfigure, and second fresh
      configure for identical target, generated test-target, and selected
      platform/backend inventories.
- [x] Cover explicit Null/headless and explicit Vulkan/Glfw configurations plus
      the supported `Auto` resolution.
- [x] Prove changing the explicit backend input changes the graph deliberately,
      while repeating an unchanged input does not.
- [x] Run focused platform/renderer CMake targets and the default CPU gate.

## Docs
- [x] Update `docs/build-troubleshooting.md` and
      `docs/architecture/test-strategy.md` with the canonical owner/order of
      backend configuration inputs.
- [x] Update task/category indexes and regenerate `tasks/SESSION-BRIEF.md` on
      retirement.

## Acceptance criteria
- [x] A clean configure and unchanged reconfigure produce byte-equivalent
      normalized target/test inventories for every covered preset identity.
- [x] No consuming CMake file reads `EXTRINSIC_PLATFORM` or
      `EXTRINSIC_BACKEND` before it is defined.
- [x] Current CPU and Vulkan test cohorts remain explicit and unchanged except
      for the deterministic correction proved by the regression.
- [x] No new backend abstraction or engine dependency edge is introduced.

## Verification
```bash
python3 tests/regression/tooling/Test.BackendConfigureDeterminism.py
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicCpuTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan --fresh
cmake --build --preset ci-vulkan --target IntrinsicGpuVulkanTests
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Completion evidence
- The pre-fix reproducer registered 26 test targets and 25 initial CPU CTest
  entries in each fresh tree, then 32 targets and 30 entries after an unchanged
  reconfigure. The corrected first clean `ci` configure registers all 32 test
  targets.
- `Test.BackendConfigureDeterminism.py` passed in 56.90 seconds and compared
  normalized Ninja targets, registered test targets and labels, CTest names,
  and all five requested/resolved cache identity fields across fresh,
  same-tree reconfigure, second-fresh, and deliberate backend-transition
  cases.
- `Test.CiTiming.py` passed 19/19 and `Test.WorkflowConcurrency.py` passed
  5/5. The timing aggregator now records the same requested/resolved graph
  identity from `CMakeCache.txt` and fails closed for an explicitly requested
  missing or incomplete cache.
- Fresh `ci` configure, `IntrinsicCpuTests`, focused `ExtrinsicPlatform` and
  `ExtrinsicGraphics` targets, and the 3,830-case default CPU gate passed.
  Fresh `ci-vulkan` configure and `IntrinsicGpuVulkanTests` also passed.

## Forbidden changes
- Relying on a warm/reconfigured build tree to obtain the intended targets.
- Pinning a different capability cohort merely to make inventories equal.
- Moving runtime backend selection into platform or renderer build code.
- Adding a second source of defaults that can drift from the top-level value.
