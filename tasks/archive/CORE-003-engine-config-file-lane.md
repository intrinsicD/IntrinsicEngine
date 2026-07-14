---
id: CORE-003
theme: F
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---
# CORE-003 — Give EngineConfig the file/preview/diagnostics config lane

## Goal
- Give `EngineConfig` the same file-load + side-effect-free preview + fail-closed
  diagnostics lane that `RenderRecipeConfig` already proves, so the engine can
  boot from a config file and so the agent/CLI lane (`RUNTIME-131`) and a
  backend-selection convention have a foundation (P3).

## Non-goals
- UI editing of the engine config (the lane must be UI-independent here;
  UI/agent facades are `RUNTIME-131`).
- Folding in `VisualizationConfig` (an ECS component) or any per-entity state.
- Reflection/registration ceremony (P1: `EngineConfig` and its sub-configs stay
  plain structs; add only a thin parse/serialize layer).
- Hot-reload of boot-only fields (partition mutability explicitly; hot subset is
  a follow-up).

## Context
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Give EngineConfig a file load lane`).
- Maturity: `CPUContracted`.
- `EngineConfig` is hand-built in C++ via `Runtime::CreateReferenceEngineConfig()`
  (`src/runtime/Runtime.Engine.cpp`, ~line 1542), passed once to the `Engine`
  ctor, and stored read-only as `m_Config`. `src/app/Sandbox/main.cpp` boots from
  that hardcoded value with no file loader, no CLI/env override, and no agent seam.
- `Extrinsic.Graphics.RenderRecipeConfig` is the reference shape to mirror:
  schema id + version, `PreviewRenderRecipeConfig` (dry-run, side-effect-free),
  `LoadRenderRecipeConfigFile`, and typed diagnostics with usable/unusable states.
- Owner/layer: the loader is a `core` config module
  (`Extrinsic.Core.Config.Engine*`); the boot wiring is `runtime`/`app`. Keep the
  value-type `EngineConfig` free of IO imports — IO lives in the loader module via
  `Core.Filesystem`/`Core.IOBackend`.

## Required changes
- [x] Add a CPU-only, deterministic, side-effect-free loader module (e.g.
      `Extrinsic.Core.Config.EngineLoad`, companion to
      `Extrinsic.Core.Config.Engine`) exposing `kEngineConfigSchemaId` +
      `kEngineConfigSchemaVersion`, an `EngineConfigState` enum, an
      `EngineConfigDiagnostic` vector, `LoadEngineConfigFile(path) -> result`,
      and `PreviewEngineConfig` (dry-run) producing a fully-defaulted
      `EngineConfig`.
- [x] Keep `EngineConfig` and its sub-configs (`SimulationConfig`, `RenderConfig`,
      `WindowConfig`) plain structs; add only the thin parse/serialize layer.
- [x] Partition mutability explicitly in the schema/docs: boot-only fields
      (graphics backend, frames-in-flight, worker-thread count, validation toggle,
      window size) vs any hot subset (deferred here).
- [x] Wire `main.cpp`/`Engine` to attempt a default config path plus an env/CLI
      override before falling back to `CreateReferenceEngineConfig()`.

## Tests
- [x] Round-trip test: a config file sets every boot field and loads to the
      expected `EngineConfig`.
- [x] Fail-closed test: invalid keys/values fall back to reference defaults with
      precise diagnostics; the loader is deterministic and side-effect-free.
- [x] Default CPU gate stays green.

## Docs
- [x] Document the engine config file schema (fields, boot-only vs hot, defaults,
      diagnostics) under `docs/architecture/` and link it from the runtime docs.
- [x] Update `src/core/README.md` for the new config module.

## Acceptance criteria
- [x] `EngineConfig` can be loaded from a file with a versioned schema and
      fail-closed diagnostics; the loader is deterministic and side-effect-free.
- [x] The value-type `EngineConfig` carries no IO imports.
- [x] `main.cpp` boots from a file/env/CLI path with a reference-default fallback.
- [x] No GPU dependency; CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicRuntimeContractTests
cmake --build --preset ci --target IntrinsicCoreWrapperUnitTests
cmake --build --preset ci --target ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -R 'CoreEngineConfigLoad|RuntimeEngineConfigBoot' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Adding reflection/registration ceremony to the config structs.
- Importing IO into the value-type `EngineConfig` module.
- Folding `VisualizationConfig` or per-entity state into the engine config.
- Implementing hot-reload of boot-only fields.

## Maturity
- Target: `CPUContracted` — the loader is fully contract-tested on the CPU gate.
- The agent/CLI facade and any hot-subset apply path are owned by `RUNTIME-131`.
- `CPUContracted` is the intended endpoint here; no `Operational` follow-up is
  owed by this task.
