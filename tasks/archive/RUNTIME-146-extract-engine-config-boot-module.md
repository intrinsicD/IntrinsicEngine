---
id: RUNTIME-146
theme: F
depends_on: []
maturity_target: Operational
completed: 2026-07-08
---
# RUNTIME-146 — Extract engine config boot into a free-standing module

## Status

- Retired on 2026-07-08 at `Operational`.
- PR: pending. Commit: pending local change.
- Runtime now owns `Extrinsic.Runtime.EngineConfigBoot` as a free-standing
  boot helper module. It exports `CreateReferenceEngineConfig()`,
  `EngineConfigBootSource`, `EngineConfigBootOptions`,
  `EngineConfigBootResult`, and `ResolveEngineConfigForBoot(...)`.
- `Runtime.Engine.cppm` and `Runtime.Engine.cpp` no longer export or define
  the boot-time config helpers, and no remaining `Engine` implementation import
  of the new module is required.
- Sandbox startup and config-control/render-recipe tests import
  `Extrinsic.Runtime.EngineConfigBoot` directly where they construct boot
  configs, preserving the CLI/env/default/reference precedence behavior.
- Verified locally with the `ci` preset, which enables
  `INTRINSIC_ENABLE_SANITIZERS=ON`: focused runtime config-boot/control tests
  passed 11/11, `IntrinsicRuntimeContractTests` and `IntrinsicTests` built, the
  full default CPU-supported CTest gate passed 3636/3636, strict layering and
  test-layout checks passed, and the module inventory was regenerated.

## Goal
- Move the boot-time config resolution surface (`EngineConfigBootSource`,
  `EngineConfigBootOptions`, `EngineConfigBootResult`,
  `ResolveEngineConfigForBoot`, `CreateReferenceEngineConfig`) out of
  `Extrinsic.Runtime.Engine` into a new free-standing
  `Extrinsic.Runtime.EngineConfigBoot` module, so config boot no longer
  requires importing the full `Engine` interface.

## Non-goals
- No behavior change to config resolution precedence
  (CLI flag → environment variable → default path → reference defaults).
- No change to `Core.Config.EngineLoad` parsing or validation.
- No compatibility re-export of the moved names from
  `Extrinsic.Runtime.Engine`; the few importers are updated directly.
- No hot-reload/config-control changes (owned by `RUNTIME-149`).

## Context
- Owning subsystem/layer: `runtime` (composition), consuming `core` config
  loading only. Config boot needs no `Engine` instance — it is a pure
  args/env/filesystem → `EngineConfig` function that today lives in the
  engine god-module for historical reasons (`CORE-003` landed it there).
- Current locations: exports in `src/runtime/Runtime.Engine.cppm`
  (`EngineConfigBoot*` types, `ResolveEngineConfigForBoot`,
  `CreateReferenceEngineConfig`); implementation plus the private helpers
  `FindCommandLineEngineConfigPath`, `FindEnvironmentEngineConfigPath`, and
  `ExistingFilePath` in `src/runtime/Runtime.Engine.cpp`.
- Known importers: `src/app/Sandbox/main.cpp` (boot path) and
  `tests/contract/runtime/Test.RuntimeEngineConfigBoot.cpp`.
- ARCH-013 re-review (2026-07-08): Decision unchanged. This is a pure
  free-standing config boot extraction with no live `Engine` instance, no
  module service requirement, and no collision with ADR-0024 seams. It should
  remain behavior-preserving and avoid becoming a generic module/config
  registry task.
- Part of the `Runtime.Engine` decomposition series
  (`RUNTIME-146..151`); this is the smallest, dependency-free slice.

## Required changes
- [x] Add `src/runtime/Runtime.EngineConfigBoot.cppm` exporting
      `EngineConfigBootSource`, `EngineConfigBootOptions`,
      `EngineConfigBootResult`, `ResolveEngineConfigForBoot`, and
      `CreateReferenceEngineConfig`, preserving the existing declarations
      verbatim.
- [x] Add `src/runtime/Runtime.EngineConfigBoot.cpp` with the moved
      implementations and the moved private path helpers.
- [x] Remove the moved declarations, definitions, and now-unused imports
      from `Runtime.Engine.cppm` / `Runtime.Engine.cpp`; the final shape leaves
      no remaining `Engine` implementation dependency on the new module.
- [x] Register the new interface unit in `src/runtime/CMakeLists.txt` via
      the existing `FILE_SET CXX_MODULES` list.
- [x] Update `src/app/Sandbox/main.cpp` and
      `tests/contract/runtime/Test.RuntimeEngineConfigBoot.cpp` to import
      `Extrinsic.Runtime.EngineConfigBoot`.

## Tests
- [x] `Test.RuntimeEngineConfigBoot.cpp` passes unchanged apart from the
      import swap (behavior-preservation evidence).
- [x] Default CPU gate stays green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [x] Regenerate the module inventory if `docs/` carries one for `runtime`
      module surfaces (per `intrinsicengine-docs-sync`).
- [x] Update `tasks/backlog/runtime/README.md` status line for this task
      on retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer exports any `EngineConfigBoot*` name.
- [x] `Extrinsic.Runtime.EngineConfigBoot` builds as its own interface unit
      and is importable without importing `Extrinsic.Runtime.Engine`.
- [x] No remaining reference to the moved names via the `Engine` module in
      `src/` or `tests/`.
- [x] CPU gate and layering check pass.

## Verification
Completed 2026-07-08:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeEngineConfigBoot.*:RuntimeConfigControlFacade.*:RuntimeRenderRecipeActivation.*'
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineConfigBoot|RuntimeConfigControlFacade|RuntimeRenderRecipeActivation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Mixing this mechanical move with semantic refactors (no signature,
  precedence, or diagnostics changes).
- Renaming existing exported types while moving them.
- Introducing unrelated feature work.

## Maturity
- Target: `Operational` — the moved surface is already exercised by the
  Sandbox boot path and its contract test; this task must preserve that,
  not add capability. No new follow-up is owed.
