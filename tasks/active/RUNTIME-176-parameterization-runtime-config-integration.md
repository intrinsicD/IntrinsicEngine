---
id: RUNTIME-176
theme: I
depends_on: [GEOM-063, METHOD-023]
maturity_target: CPUContracted
---
# RUNTIME-176 — Parameterization runtime facade, config lane, and UV view model

## Goal
- Wire the CPU parameterization surface into the engine end-to-end: a runtime
  command that writes selected-mesh UVs back as `v:texcoord`, a serializable
  config section selecting only implemented strategies through one validated
  config/agent/UI lane, and a pointer-free CPU UV view model for `UI-036`.

## Non-goals
- No algorithm code — this task consumes `Geometry.Parameterization`
  (`GEOM-063` + `METHOD-023`); it does not implement strategies.
- No ImGui panel or 2D drawing here — `UI-036` owns presentation.
- No optimized/GPU token, fallback adapter, or job queue. METHOD-025/026 add
  those control values and implementations for ARAP/SLIM when they land.
- No new second viewport/camera or renderer change — the UV view is delivered by `UI-036` from this task's view model (see `ADR-0025`); a GPU-rendered UV target is the optional `GRAPHICS-122` upgrade.

## Context
- Owner/layer: `src/runtime` (composition/wiring) plus a config value struct in `src/core` (`Core.Config.Engine`). `runtime -> all lower layers`; `core -> nothing`.
- Template to mirror: `RUNTIME-134` and the existing progressive-Poisson
  editor config command for the config lane,
  runtime command DTO, and ECS-property writeback. Their backend toggles are not
  copied because this task has only a CPU implementation.
- Config lane (`AGENTS.md` §5 P3): the engine tree is `EngineConfig { …, SandboxConfig }` in `Core.Config.Engine.cppm`; the file lane is `Core.Config.EngineLoad` (schema `intrinsic.core.engine-config`); the runtime facade is `Extrinsic.Runtime.EngineConfigControl` via `Engine::GetConfigControl()`, whose `RuntimeConfigControlSource { Editor, AgentCli, Programmatic }` tag is the co-equal-surfaces mechanism.
- Editor facade pattern: `ApplySandboxEditor<X>Command` in `Runtime.SandboxEditorFacades.cpp` / `Runtime.SandboxMethodFacade.cpp`, reading geometry via `GS::BuildConstView(raw, entity)` (require `Domain::Mesh`) and writing UVs back through the promoted `v:texcoord` path (`Runtime.MeshGeometryPacker` packs `{position.xyz, U, V}`; RUNTIME-108/GRAPHICS-088 already upload resolved UVs) plus `Dirty::MarkVertexTexcoordsDirty` (`ECS.Component.DirtyTags`), wrapped in `EditorCommandHistory` for undo — mirror the UV-atlas regeneration command from retired `UI-014`.
- UV view model precedent: `SandboxEditorUvDiagnosticsModel`
  (`Runtime.SandboxEditorFacades.cppm`) is already a runtime-built,
  pointer-free UI model for UV state. This task adds a focused
  `SandboxEditorParameterizationViewModel` carrying per-vertex UVs, triangle
  connectivity, finite bounds, and the aggregate diagnostics actually exposed
  by `ParameterizeResult`; chart/seam and per-face-distortion records are not
  invented.
- `ARCH-006` retired mesh-processing presentation into `src/app/Sandbox`; land the runtime seams so the app-owned panel (`UI-036`) consumes them without owning engine state.

## Control surfaces
- Config: `EngineConfig.sandbox.parameterization` with explicit stable tokens
  `lscm`, `harmonic_cotangent`, `tutte_uniform`, and `bff`, plus concrete
  parameter records for the strategies implemented at this task's landing;
  round-tripped by `Core.Config.EngineLoad` and applied as a hot subset. Never
  serialize `std::variant::index()`.
- UI: Sandbox parameterization editor panel + UV split view (owned by `UI-036`) drives the same validated apply path.
- Agent/CLI: `config/engine.json` + the `--engine-config <path>` boot flag, and programmatic `Engine::GetConfigControl().LoadAndApplyEngineConfigHotSubsetFile(...)` / `ApplyEngineConfigHotSubset(...)` tagged `AgentCli`/`Programmatic`.

## Backends
- CPU reference only; no backend selector is exposed. METHOD-025/026 extend the
  config and result model when real optimized/GPU implementations exist.

## Status
- In progress on branch `codex/arch-006-completion`; owner: Codex.
- Dependencies `GEOM-063` and `METHOD-023` are retired. Promotion right-sized
  the task to the existing config-control and sandbox-editor seams.
- Slice 1: land the core value schema, stable JSON tokens, parser/serializer,
  validation, hot-subset equality/copy, and config tests.
- Slice 2: land the same-module runtime facade, undoable UV writeback,
  configured command path, pointer-free view model, and contract tests.
- Slice 3: synchronize architecture/method docs and inventories, run the
  default CPU and architecture-review gates, and retire at `CPUContracted`.
- Next verification: strict task/state validation after promotion, then the
  focused core config test build for slice 1.

## Required changes
- [ ] Add a `ParameterizationConfig` value struct nested in `SandboxConfig`:
      explicit strategy/mode enums whose JSON tokens are stable, typed records
      for LSCM pins/solver values, harmonic boundary/pins, and BFF boundary
      controls; deterministic defaults and no variant-index serialization.
- [ ] Extend `Core.Config.EngineLoad.cpp` parser/serializer and the schema table (and `docs/architecture/engine-config.md`) to round-trip the section with diagnostics; add it to the `EngineConfigControl` hot-apply subset.
- [ ] Add `ApplySandboxEditorParameterizationCommand` (+ config-routed variant):
      read the selected mesh, convert its stable strategy token to the typed
      geometry payload, run `ParameterizeMesh`, write `v:texcoord`, mark it
      dirty, and record an undoable history entry.
- [ ] Export a `SandboxEditorParameterizationResult` with chosen stable strategy
      token, status, and diagnostics summary — no raw geometry pointers.
- [ ] Export a `SandboxEditorParameterizationViewModel` (per-vertex UVs,
      triangle index triples, UV bounds, and aggregate last-result diagnostics)
      built from the selected mesh + last result for `UI-036`; pointer-free and
      copyable.
- [ ] Add the implementation as a private source of the existing
      `Extrinsic.Runtime.SandboxEditorFacades` module and expose free functions
      through the existing engine/session context. Do not add a service,
      registry, policy object, or new module for one implementation.

## Tests
- [ ] Extend `tests/unit/core/Test.Core.EngineConfigLoad.cpp` (`unit;core`): the
      config section round-trips through `Core.Config.EngineLoad` (parse →
      serialize → parse is stable), preserves vectors/pairs, and rejects
      invalid tokens, ranges, and paired-array mismatches with diagnostics.
- [ ] `tests/contract/runtime/Test.ParameterizationFacade.cpp` (`contract;runtime`,
      Null device): each implemented strategy writes finite `v:texcoord`, marks
      texcoords dirty, and is undoable; unsupported config tokens fail closed.
- [ ] View-model build: given a parameterized mesh,
      `SandboxEditorParameterizationViewModel` has UV count == vertex storage
      count, deterministic triangle triples, finite UV bounds, and aggregate
      diagnostics populated — with no raw pointers.
- [ ] Apply-source parity: applying the same validated config via `Editor`,
      `AgentCli`, and `Programmatic` sources produces identical config state;
      the configured facade then produces identical UVs.
- [ ] Determinism: identical config + input produce identical UVs.

## Docs
- [ ] Update `docs/architecture/engine-config.md` and `docs/architecture/runtime-config-control.md` with the new section and hot-apply entry.
- [ ] Add a short runtime-integration note to the parameterization method-package READMEs pointing at the runtime facade and config path.
- [ ] Refresh `docs/api/generated/module_inventory.md` if the exported import
      surface changes; this task adds no new module.

## Acceptance criteria
- [ ] A selected mesh is parameterized and its `v:texcoord` property is updated
      through the runtime facade under Null/default runtime paths.
- [ ] Every implemented strategy is choosable from config, agent/programmatic
      apply, and UI through one validated path with source tagging.
- [ ] The UV view model is produced for `UI-036` and is pointer-free.
- [ ] Contract tests pass in the default CPU gate; layering holds (`runtime -> lower`, `core -> nothing`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization|EngineConfig' -LE 'gpu|vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No algorithm/strategy code in runtime (consume `Geometry.Parameterization`).
- No UI-only control path — every knob must round-trip through the config lane.
- No placeholder optimized/GPU selector or adapter in this CPU-only task.
- No second camera/viewport or renderer contract change in this task (see `ADR-0025`; the optional GPU-rendered UV target is `GRAPHICS-122`).

## Maturity
- Target: `CPUContracted` — the CPU facade, config lane, undoable writeback,
  and pointer-free view model are covered under Null/default runtime contracts.
  `Operational` owned by `UI-036`, which supplies the visible sandbox proof.
  METHOD-025/026 own later optimized/GPU extensions only when those
  implementations exist.
