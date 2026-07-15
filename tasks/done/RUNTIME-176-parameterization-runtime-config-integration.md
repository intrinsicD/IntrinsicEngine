---
id: RUNTIME-176
theme: I
depends_on: [GEOM-063, METHOD-023]
maturity_target: CPUContracted
completed_on: 2026-07-15
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
- Completed on 2026-07-15 at `CPUContracted` on branch
  `codex/arch-006-completion`; implementation commit: `e8c3f73e`.
  Dependencies `GEOM-063` and `METHOD-023` are retired.
- Core config now round-trips stable LSCM, harmonic/Tutte, and BFF values and
  hot-applies the block with `SandboxParameterizationChanged`. The runtime
  facade converts the active config to the typed geometry variant, writes
  storage-aligned `v:texcoord` values with undo/redo, preserves deleted vertex
  tombstones, and exposes the pointer-free UV/triangle/diagnostics view model.
- Static token, range, pairing, narrowing, and BFF closure errors fail closed.
  Mesh-dependent custom harmonic pin coverage/validity remains geometry-owned
  and returns a normalized failure without mutating UV state.
- Focused verification: `IntrinsicRuntimeContractTests` built with ASan/UBSan;
  `ParameterizationFacade.*` passed 13/13, and the combined
  `Parameterization|EngineConfig` selection passed 56/56.
- Merge-quality verification: `IntrinsicTests` built through the `ci` preset;
  the default CPU gate passed 3,743/3,743 tests in 380.43 seconds. Strict
  layering, allowlist quality, test layout, task policy/state/maturity, docs
  links (2,677), and `git diff --check` are clean. The module inventory was
  regenerated (391 modules) and produced no content change.
- Independent architecture/right-sizing review found no blockers after two
  fixes: persistent history closures no longer retain the session-owned model
  cache, and reconstructed solver meshes restore deleted vertex slots. The
  clean-workshop scorecard records rows 1-3 and 7 `pass`, rows 4-6 and 8
  `n/a`, and no findings.
- One early Clang 23/ccache module-frontend bus error was ruled out via the
  stale-build triage: a ccache-disabled rebuild succeeded, and subsequent
  normal cached focused and full builds passed. The technical implementation,
  evidence, documentation, and retirement record are complete.

## Required changes
- [x] Add a `ParameterizationConfig` value struct nested in `SandboxConfig`:
      explicit strategy/mode enums whose JSON tokens are stable, typed records
      for LSCM pins/solver values, harmonic boundary/pins, and BFF boundary
      controls; deterministic defaults and no variant-index serialization.
- [x] Extend `Core.Config.EngineLoad.cpp` parser/serializer and the schema table (and `docs/architecture/engine-config.md`) to round-trip the section with diagnostics; add it to the `EngineConfigControl` hot-apply subset.
- [x] Add `ApplySandboxEditorParameterizationCommand` (+ config-routed variant):
      read the selected mesh, convert its stable strategy token to the typed
      geometry payload, run `ParameterizeMesh`, write `v:texcoord`, mark it
      dirty, and record an undoable history entry.
- [x] Export a `SandboxEditorParameterizationResult` with chosen stable strategy
      token, status, and diagnostics summary — no raw geometry pointers.
- [x] Export a `SandboxEditorParameterizationViewModel` (per-vertex UVs,
      triangle index triples, UV bounds, and aggregate last-result diagnostics)
      built from the selected mesh + last result for `UI-036`; pointer-free and
      copyable.
- [x] Add the implementation as a private source of the existing
      `Extrinsic.Runtime.SandboxEditorFacades` module and expose free functions
      through the existing engine/session context. Do not add a service,
      registry, policy object, or new module for one implementation.

## Tests
- [x] Extend `tests/unit/core/Test.Core.EngineConfigLoad.cpp` (`unit;core`): the
      config section round-trips through `Core.Config.EngineLoad` (parse →
      serialize → parse is stable), preserves vectors/pairs, and rejects
      invalid tokens, ranges, and paired-array mismatches with diagnostics.
- [x] `tests/contract/runtime/Test.ParameterizationFacade.cpp` (`contract;runtime`,
      Null device): each implemented strategy writes finite `v:texcoord`, marks
      texcoords dirty, and is undoable; unsupported config tokens fail closed.
- [x] View-model build: given a parameterized mesh,
      `SandboxEditorParameterizationViewModel` has UV count == vertex storage
      count, deterministic triangle triples, finite UV bounds, and aggregate
      diagnostics populated — with no raw pointers.
- [x] Apply-source parity: applying the same validated config via `Editor`,
      `AgentCli`, and `Programmatic` sources produces identical config state;
      the configured facade then produces identical UVs.
- [x] Determinism: identical config + input produce identical UVs.

## Docs
- [x] Update `docs/architecture/engine-config.md` and `docs/architecture/runtime-config-control.md` with the new section and hot-apply entry.
- [x] Add a short runtime-integration note to the parameterization method-package READMEs pointing at the runtime facade and config path.
- [x] Refresh `docs/api/generated/module_inventory.md` if the exported import
      surface changes; this task adds no new module.

## Acceptance criteria
- [x] A selected mesh is parameterized and its `v:texcoord` property is updated
      through the runtime facade under Null/default runtime paths.
- [x] Every implemented strategy is choosable from config, agent/programmatic
      apply, and UI through one validated path with source tagging.
- [x] The UV view model is produced for `UI-036` and is pointer-free.
- [x] Contract tests pass in the default CPU gate; layering holds (`runtime -> lower`, `core -> nothing`).

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
