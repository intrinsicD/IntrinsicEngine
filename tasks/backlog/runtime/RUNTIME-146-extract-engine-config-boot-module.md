---
id: RUNTIME-146
theme: F
depends_on: []
maturity_target: Operational
---
# RUNTIME-146 — Extract engine config boot into a free-standing module

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
- Part of the `Runtime.Engine` decomposition series
  (`RUNTIME-146..151`); this is the smallest, dependency-free slice.

## Required changes
- [ ] Add `src/runtime/Runtime.EngineConfigBoot.cppm` exporting
      `EngineConfigBootSource`, `EngineConfigBootOptions`,
      `EngineConfigBootResult`, `ResolveEngineConfigForBoot`, and
      `CreateReferenceEngineConfig`, preserving the existing declarations
      verbatim.
- [ ] Add `src/runtime/Runtime.EngineConfigBoot.cpp` with the moved
      implementations and the moved private path helpers.
- [ ] Remove the moved declarations, definitions, and now-unused imports
      from `Runtime.Engine.cppm` / `Runtime.Engine.cpp`;
      `Runtime.Engine.cpp` imports the new module for its remaining
      internal uses (reference defaults during `Initialize()`).
- [ ] Register the new interface unit in `src/runtime/CMakeLists.txt` via
      the existing `FILE_SET CXX_MODULES` list.
- [ ] Update `src/app/Sandbox/main.cpp` and
      `tests/contract/runtime/Test.RuntimeEngineConfigBoot.cpp` to import
      `Extrinsic.Runtime.EngineConfigBoot`.

## Tests
- [ ] `Test.RuntimeEngineConfigBoot.cpp` passes unchanged apart from the
      import swap (behavior-preservation evidence).
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [ ] Regenerate the module inventory if `docs/` carries one for `runtime`
      module surfaces (per `intrinsicengine-docs-sync`).
- [ ] Update `tasks/backlog/runtime/README.md` status line for this task
      on retirement.

## Acceptance criteria
- [ ] `Runtime.Engine.cppm` no longer exports any `EngineConfigBoot*` name.
- [ ] `Extrinsic.Runtime.EngineConfigBoot` builds as its own interface unit
      and is importable without importing `Extrinsic.Runtime.Engine`.
- [ ] No remaining reference to the moved names via the `Engine` module in
      `src/` or `tests/`.
- [ ] CPU gate and layering check pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
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
